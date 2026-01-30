#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>


// =======================================================
// Session store (your own tokens)
// =======================================================

struct Session
{
    std::string userName; // use email as identity
    std::string playerName; // use email as identity
    std::chrono::system_clock::time_point expiresAt;
};

extern std::unordered_map<std::string, Session> g_sessions;
extern std::mutex g_sessionsMutex;

// helpers
bool TryGetSession(const std::string& token, Session& out);
void RemoveSession(const std::string& token);
