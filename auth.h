#pragma once
#include "httplib.h"
#include <string>
#include <functional>
#include "sessions.h"

struct Session;

// Called after a session was created/updated successfully.
// Return false to indicate "non-fatal init failed" (optional).
using PostLoginHook = std::function<bool(Session&)>;

void SetPostLoginHook(PostLoginHook hook);


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

