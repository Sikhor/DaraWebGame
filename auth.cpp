#include "auth.h"

#include <cpr/cpr.h>
#include "json.hpp"

// jwt-cpp (you must add this dependency)
#include <jwt-cpp/jwt.h>

#include <unordered_map>
#include <mutex>
#include <chrono>
#include <random>
#include <sstream>
#include <regex>
#include <cstdlib>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include "sessions.h"
using json = nlohmann::json;



// --- base64url decode helper ---
static std::string Base64UrlToBase64(std::string s) {
    for (auto& c : s) { if (c == '-') c = '+'; else if (c == '_') c = '/'; }
    while (s.size() % 4) s.push_back('=');
    return s;
}

static std::vector<unsigned char> Base64Decode(const std::string& b64) {
    BIO* bmem = BIO_new_mem_buf(b64.data(), (int)b64.size());
    BIO* b64bio = BIO_new(BIO_f_base64());
    BIO_set_flags(b64bio, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(b64bio, bmem);

    std::vector<unsigned char> out(b64.size());
    int len = BIO_read(bmem, out.data(), (int)out.size());
    BIO_free_all(bmem);

    if (len <= 0) return {};
    out.resize(len);
    return out;
}

static std::string JwkNeToPem(const std::string& n_b64url, const std::string& e_b64url) {
    auto n_bin = Base64Decode(Base64UrlToBase64(n_b64url));
    auto e_bin = Base64Decode(Base64UrlToBase64(e_b64url));
    if (n_bin.empty() || e_bin.empty()) return {};

    BIGNUM* n = BN_bin2bn(n_bin.data(), (int)n_bin.size(), nullptr);
    BIGNUM* e = BN_bin2bn(e_bin.data(), (int)e_bin.size(), nullptr);
    if (!n || !e) { if(n) BN_free(n); if(e) BN_free(e); return {}; }

    RSA* rsa = RSA_new();
    if (!rsa) { BN_free(n); BN_free(e); return {}; }

    // RSA_set0_key takes ownership of n,e
    if (RSA_set0_key(rsa, n, e, nullptr) != 1) {
        RSA_free(rsa); // frees n,e too if set0 succeeded; here it didn't, so:
        BN_free(n); BN_free(e);
        return {};
    }

    BIO* mem = BIO_new(BIO_s_mem());
    // write as SubjectPublicKeyInfo (-----BEGIN PUBLIC KEY-----)
    if (PEM_write_bio_RSA_PUBKEY(mem, rsa) != 1) {
        BIO_free(mem);
        RSA_free(rsa);
        return {};
    }

    char* data = nullptr;
    long len = BIO_get_mem_data(mem, &data);
    std::string pem(data, (size_t)len);

    BIO_free(mem);
    RSA_free(rsa);
    return pem;
}
// =======================================================
// Config (env)
// =======================================================

static std::string GetEnvOrThrow(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v) throw std::runtime_error(std::string(name) + " not set");
    std::cerr << "Google Client Id: " << v << std::endl;
    std::cerr.flush();
    return std::string(v);
}

static std::string GetEnvOrDefault(const char* name, const std::string& def)
{
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    return std::string(v);
}

// Required:
//   GOOGLE_CLIENT_ID=xxxxx.apps.googleusercontent.com
// Optional:
//   GOOGLE_JWKS_URL=https://www.googleapis.com/oauth2/v3/certs
static std::string GoogleClientId()
{
    return GetEnvOrThrow("GOOGLE_CLIENT_ID");
}

static std::string GoogleJwksUrl()
{
    return GetEnvOrDefault("GOOGLE_JWKS_URL", "https://www.googleapis.com/oauth2/v3/certs");
}



// Adjust as you like
static constexpr int kSessionDays = 7;

static std::string MakeToken()
{
    static thread_local std::mt19937_64 rng{ std::random_device{}() };
    std::uniform_int_distribution<uint64_t> dist;

    uint64_t a = dist(rng);
    uint64_t b = dist(rng);

    std::ostringstream oss;
    oss << std::hex << a << b;
    return oss.str();
}

void AddCorsHeadersAuth(httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
}

std::string GetBearerToken(const httplib::Request& req)
{
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return {};
    const std::string& v = it->second;
    const std::string prefix = "Bearer ";
    if (v.rfind(prefix, 0) != 0) return {};
    return v.substr(prefix.size());
}

// =======================================================
// Session Helper 
// =======================================================


bool GetSessionFromRequest(
    const httplib::Request& req,
    Session& outSession,
    std::string* outErr)
{
    const std::string token = GetBearerToken(req);
    if (token.empty()) {
        if (outErr) *outErr = "Missing Bearer token";
        return false;
    }

    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) {
        if (outErr) *outErr = "Invalid session";
        return false;
    }

    if (it->second.expiresAt < std::chrono::system_clock::now()) {
        g_sessions.erase(it);
        if (outErr) *outErr = "Session expired";
        return false;
    }

    outSession = it->second;
    return true;
}



bool ValidateSessionToken(const std::string& token, std::string& outUserName)
{
    if (token.empty()) return false;

    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) return false;

    const auto now = std::chrono::system_clock::now();
    if (it->second.expiresAt < now)
    {
        g_sessions.erase(it);
        return false;
    }

    outUserName = it->second.userName;
    return true;
}

// =======================================================
// Google cert (JWKS) cache
// =======================================================

// We store "kid" -> PEM certificate (x5c converted to PEM).
static std::unordered_map<std::string, std::string> g_googleKidToPem;
static std::mutex g_googleCertMutex;
static std::chrono::system_clock::time_point g_googleCertNextRefresh =
    std::chrono::system_clock::time_point::min();

static int ParseMaxAgeSeconds(const cpr::Response& r)
{
    // Parse Cache-Control: public, max-age=XXXX, must-revalidate, ...
    auto it = r.header.find("cache-control");
    if (it == r.header.end()) return 300; // default fallback: 5 minutes

    std::smatch m;
    std::regex re(R"(max-age\s*=\s*(\d+))", std::regex::icase);
    if (std::regex_search(it->second, m, re) && m.size() >= 2)
    {
        try { return std::stoi(m[1].str()); }
        catch (...) { return 300; }
    }
    return 300;
}

static std::string X5cToPemCert(const std::string& x5cBase64)
{
    // Convert x5c base64 to PEM cert format
    // (wrap lines at 64 chars for readability)
    std::ostringstream oss;
    oss << "-----BEGIN CERTIFICATE-----\n";
    for (size_t i = 0; i < x5cBase64.size(); i += 64)
        oss << x5cBase64.substr(i, 64) << "\n";
    oss << "-----END CERTIFICATE-----\n";
    return oss.str();
}

static bool RefreshGoogleCerts(std::string* outErr)
{
    try
    {
        const std::string url = GoogleJwksUrl();

        cpr::Response r = cpr::Get(
            cpr::Url{url},
            cpr::Timeout{8000}
        );

        if (r.status_code != 200)
        {
            if (outErr) *outErr = "Failed to fetch Google certs (HTTP " + std::to_string(r.status_code) + ")";
            return false;
        }

        json j = json::parse(r.text);

        if (!j.contains("keys") || !j["keys"].is_array())
        {
            if (outErr) *outErr = "Google certs response missing 'keys'";
            return false;
        }

        std::unordered_map<std::string, std::string> nextMap;

        for (const auto& key : j["keys"])
        {
            if (!key.contains("kid") || !key["kid"].is_string()) continue;
            const std::string kid = key["kid"].get<std::string>();

            // 1) Prefer x5c if present
            if (key.contains("x5c") && key["x5c"].is_array() && !key["x5c"].empty() && key["x5c"][0].is_string())
            {
                nextMap[kid] = X5cToPemCert(key["x5c"][0].get<std::string>());
                continue;
            }

            // 2) Fallback: n/e
            if (key.contains("n") && key.contains("e") && key["n"].is_string() && key["e"].is_string())
            {
                std::string pem = JwkNeToPem(key["n"].get<std::string>(), key["e"].get<std::string>());
                if (!pem.empty())
                    nextMap[kid] = pem;
                continue;
            }
        }

        if (nextMap.empty())
        {
            if (outErr) *outErr = "Google certs parsed but no usable keys found";
            return false;
        }

        const int maxAge = ParseMaxAgeSeconds(r);
        const auto nextRefresh = std::chrono::system_clock::now() + std::chrono::seconds(maxAge);

        {
            std::lock_guard<std::mutex> lk(g_googleCertMutex);
            g_googleKidToPem = std::move(nextMap);
            g_googleCertNextRefresh = nextRefresh;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        if (outErr) *outErr = std::string("RefreshGoogleCerts exception: ") + e.what();
        return false;
    }
}

static bool EnsureGoogleCertsFresh(std::string* outErr)
{
    // Fast path check
    {
        std::lock_guard<std::mutex> lk(g_googleCertMutex);
        if (!g_googleKidToPem.empty() && std::chrono::system_clock::now() < g_googleCertNextRefresh)
            return true;
    }

    // Refresh
    return RefreshGoogleCerts(outErr);
}

// =======================================================
// Google ID token verification
// =======================================================

struct GoogleClaims
{
    std::string sub;
    std::string eMail;
    bool email_verified = false;
    std::string name;
    std::string picture;
    std::string issuer;
};

static bool VerifyGoogleIdToken(const std::string& idToken, GoogleClaims& out, std::string* outErr)
{
    try
    {
        // Decode without verification first (to read header kid)
        auto decoded = jwt::decode(idToken);

        std::string kid;
        try {
            kid = decoded.get_header_claim("kid").as_string();
        } catch (...) {
            if (outErr) *outErr = "Missing JWT header 'kid'";
            return false;
        }

        // Ensure we have certs cached
        if (!EnsureGoogleCertsFresh(outErr))
            return false;

        std::string pemCert;
        {
            std::lock_guard<std::mutex> lk(g_googleCertMutex);
            auto it = g_googleKidToPem.find(kid);
            if (it != g_googleKidToPem.end())
                pemCert = it->second;
        }

        // If kid missing, refresh once and try again (rotation)
        if (pemCert.empty())
        {
            std::string refreshErr;
            RefreshGoogleCerts(&refreshErr);

            std::lock_guard<std::mutex> lk(g_googleCertMutex);
            auto it = g_googleKidToPem.find(kid);
            if (it == g_googleKidToPem.end())
            {
                if (outErr) *outErr = "Unknown 'kid' even after refresh";
                return false;
            }
            pemCert = it->second;
        }

        // Verify signature + issuer + audience
        const std::string aud = GoogleClientId();

        // Google issuer can be "https://accounts.google.com" OR "accounts.google.com"
        // jwt-cpp verifier supports only one issuer check, so we do manual check after verify,
        // but we still check aud here.
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::rs256(pemCert,"", "", "")) // verify using cert/public key
            .with_audience(aud);

        verifier.verify(decoded);

        // Now validate issuer manually (because issuer can be one of two)
        std::string iss;
        try { iss = decoded.get_payload_claim("iss").as_string(); }
        catch (...) { iss.clear(); }

        if (!(iss == "https://accounts.google.com" || iss == "accounts.google.com"))
        {
            if (outErr) *outErr = "Invalid issuer";
            return false;
        }

        // Extract claims
        out.issuer = iss;

        try { out.sub = decoded.get_payload_claim("sub").as_string(); }
        catch (...) {}

        // email can be missing depending on scopes / configuration; for Google Sign-In it’s usually present
        try { out.eMail = decoded.get_payload_claim("email").as_string(); }
        catch (...) {}

        try { out.email_verified = decoded.get_payload_claim("email_verified").as_boolean(); }
        catch (...) { out.email_verified = false; }

        try { out.name = decoded.get_payload_claim("name").as_string(); }
        catch (...) {}

        try { out.picture = decoded.get_payload_claim("picture").as_string(); }
        catch (...) {}

        // Enforce "Google-only" and "verified email" identity
        if (out.eMail.empty())
        {
            if (outErr) *outErr = "Token missing email claim";
            return false;
        }
        if (!out.email_verified)
        {
            if (outErr) *outErr = "Email not verified";
            return false;
        }

        return true;
    }
    catch (const std::exception& e)
    {
        if (outErr) *outErr = std::string("Verify failed: ") + e.what();
        return false;
    }
}

// =======================================================
// Routes
// =======================================================

void RegisterAuthRoutes(httplib::Server& server)
{
    // Preflight
    server.Options("/auth/google", [](const httplib::Request&, httplib::Response& res){
        AddCorsHeadersAuth(res);
        res.status = 204;
    });
    server.Options("/logout", [](const httplib::Request&, httplib::Response& res){
        AddCorsHeadersAuth(res);
        res.status = 204;
    });
    server.Options("/me", [](const httplib::Request&, httplib::Response& res){
        AddCorsHeadersAuth(res);
        res.status = 204;
    });

    // Google login
    server.Post("/auth/google", [](const httplib::Request& req, httplib::Response& res){
        AddCorsHeadersAuth(res);

        try
        {
            auto body = json::parse(req.body);
            std::string idToken = body.at("idToken").get<std::string>();

            GoogleClaims claims;
            std::string err;
            if (!VerifyGoogleIdToken(idToken, claims, &err))
            {
                res.status = 401;
                res.set_content((json{{"status","error"},{"message",err}}).dump(), "application/json");
                return;
            }

            // Create your own session token
            const std::string token = MakeToken();
            Session s;
            s.sub= claims.sub;
            s.userName = claims.eMail;
            s.eMail= claims.eMail;
            s.name= claims.name;

            s.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(24 * kSessionDays);
            {
                std::lock_guard<std::mutex> lk(g_sessionsMutex);
                g_sessions[token] = s;
            }

            json out = {
                {"status","ok"},
                {"token", token},
                {"userName", s.userName},
                {"name", claims.name},
                {"playerName", s.playerName},
                {"picture", claims.picture},
                {"expiresDays", kSessionDays}
            };

            res.status = 200;
            res.set_content(out.dump(), "application/json");
        }
        catch (...)
        {
            res.status = 400;
            res.set_content(R"({"status":"error","message":"Invalid JSON"})", "application/json");
        }
    });

    // Logout (invalidate your token)
    server.Post("/logout", [](const httplib::Request& req, httplib::Response& res){
        AddCorsHeadersAuth(res);

        const std::string token = GetBearerToken(req);
        if (token.empty())
        {
            res.status = 401;
            res.set_content(R"({"status":"error","message":"Missing Bearer token"})", "application/json");
            return;
        }

        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions.erase(token);
        }

        res.status = 200;
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // Debug helper (who am I)
    server.Get("/me", [](const httplib::Request& req, httplib::Response& res){
     AddCorsHeadersAuth(res);

        const std::string token = GetBearerToken(req);
        if (token.empty()) { 
            res.status = 401;
            res.set_content(R"({"status":"error","message":"Missing Bearer token"})", "application/json");
            return;
         }

        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        auto it = g_sessions.find(token);
        if (it == g_sessions.end()) {
            res.status = 401;
            res.set_content(R"({"status":"error","message":"Invalid session"})", "application/json");
            return;
        }
        res.status = 200;
        res.set_content((json{
            {"status","ok"},
            {"userName", it->second.userName},
            {"playerName", it->second.playerName}
        }).dump(), "application/json");
    });
}
