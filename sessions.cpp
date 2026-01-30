#include "sessions.h"

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
