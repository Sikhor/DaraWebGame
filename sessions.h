#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>


// =======================================================
// Session store (your own tokens)
// =======================================================

struct Session
{
    std::string characterId;     // currently selected character
    std::string characterName;   // convenience: cached from character list 
    std::string sub; // should be the id from google
    std::string userName; // use email as identity if you do not use sub.. sub would be better
    std::string name; 
    std::string eMail; // should be the email
    std::string playerName; // use email as identity
    std::chrono::system_clock::time_point expiresAt;
};

extern std::unordered_map<std::string, Session> g_sessions;
extern std::mutex g_sessionsMutex;

// helpers
bool TryGetSession(const std::string& token, Session& out);
void RemoveSession(const std::string& token);

// Update the session stored under token. Returns false if token not found.
bool UpdateSessionByToken(const std::string& token, const Session& updated);
bool UpdateSessionByToken(const std::string& token,
                          const std::function<void(Session&)>& fn);


// Convenience: patch only selected character fields (recommended)
bool SetSessionCharacter(const std::string& token,
                         const std::string& characterId,
                         const std::string& characterName);
