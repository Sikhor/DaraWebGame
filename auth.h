#pragma once
#include "httplib.h"
#include <string>

// =======================================================
// Session store (your own tokens)
// =======================================================

struct Session
{
    std::string userName; // use email as identity
    std::string playerName; // use email as identity
    std::chrono::system_clock::time_point expiresAt;
};



// Register /auth/google, /logout, /me
void RegisterAuthRoutes(httplib::Server& server);

// CORS helper (includes Authorization header)
void AddCorsHeadersAuth(httplib::Response& res);

// Extract "Bearer <token>" from Authorization header
std::string GetBearerToken(const httplib::Request& req);

// Validate your own session token (returned by /auth/google)
bool ValidateSessionToken(const std::string& token, std::string& outUserName);

// used in main to get infos from token ?
bool GetSessionFromRequest(
    const httplib::Request& req,
    Session& outSession,
    std::string* outErr = nullptr); 

// callback when a new player is created    
using OnNewPlayerFn = std::function<std::string(const std::string&, const std::string&, const std::string&)>;

void SetOnNewPlayerCallback(OnNewPlayerFn cb);