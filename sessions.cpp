#include "sessions.h"
#include <functional>

std::unordered_map<std::string, Session> g_sessions;
std::mutex g_sessionsMutex;

bool TryGetSession(const std::string& token, Session& out) {
  std::lock_guard<std::mutex> lk(g_sessionsMutex);
  auto it = g_sessions.find(token);
  if (it == g_sessions.end()) return false;
  out = it->second;
  return true;
}

void RemoveSession(const std::string& token) {
  std::lock_guard<std::mutex> lk(g_sessionsMutex);
  g_sessions.erase(token);
}

// -------------------------------
// NEW: Update full session object
// -------------------------------
bool UpdateSessionByToken(const std::string& token, const Session& updated)
{
  std::lock_guard<std::mutex> lk(g_sessionsMutex);
  auto it = g_sessions.find(token);
  if (it == g_sessions.end()) return false;
  it->second = updated;
  return true;
}

// ----------------------------------------
// NEW: Patch-style update (recommended)
// ----------------------------------------
bool PatchSessionByToken(const std::string& token,
                         const std::function<void(Session&)>& fn)
{
  std::lock_guard<std::mutex> lk(g_sessionsMutex);
  auto it = g_sessions.find(token);
  if (it == g_sessions.end()) return false;
  fn(it->second);
  return true;
}

// ----------------------------------------
// NEW: Set selected character on session
// ----------------------------------------
bool SetSessionCharacter(const std::string& token,
                         const std::string& characterId,
                         const std::string& characterName)
{
  return PatchSessionByToken(token, [&](Session& s){
    s.characterId = characterId;       // you need these fields in Session
    s.characterName = characterName;   // optional but handy for UI
    s.playerName = characterName;      // if your CombatDirector uses playerName as “active character”
  });
}
