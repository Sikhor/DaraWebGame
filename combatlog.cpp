#include "combatlog.h"
#include <algorithm>

auto round2 = [](float v) {
    return std::round(v * 100.f) / 100.f;
};


CombatLog::CombatLog(std::unordered_map<std::string, CombatantPtr>& players,
                     std::mutex& playersMutex,
                     std::unordered_map<std::string, CombatantPtr>& mobs,
                     std::mutex& mobsMutex)
    : Players(players)
    , PlayersMutex(playersMutex)
    , Mobs(mobs)
    , MobsMutex(mobsMutex)
{}

void CombatLog::SetMaxEntriesPerEntity(std::size_t maxEntries)
{
    std::lock_guard<std::mutex> lk(LogMutex);
    MaxEntriesPerEntity = std::max<std::size_t>(1, maxEntries);
}

void CombatLog::PushPlayerLog(const std::string& playerName, const std::string& msg, const std::string& action)
{
    std::lock_guard<std::mutex> lk(LogMutex);
    MarkDirty();
    PlayerLast[playerName] = {msg, action};
}

void CombatLog::PushMobLog(const std::string& mobName, const std::string& msg, const std::string& action)
{
    std::lock_guard<std::mutex> lk(LogMutex);
    MarkDirty();
    MobLast[mobName] = {msg, action};
}

json CombatLog::BuildJson() const
{
    // Lock order: always lock state mutexes in same order to avoid deadlocks.
    // We'll lock Players, then Mobs, then Log.
    std::scoped_lock stateLock(PlayersMutex, MobsMutex);
    std::lock_guard<std::mutex> logLock(LogMutex);

    json out;
    out["Players"] = json::array();
    out["Mobs"] = json::array();

    // Players
    for (const auto& [name, ptr] : Players)
    {
        if (!ptr) continue;

        auto it = PlayerLast.find(name);
        const std::string msg    = (it != PlayerLast.end()) ? it->second.first  : "";
        const std::string action = (it != PlayerLast.end()) ? it->second.second : "";

        out["Players"].push_back({
            {"player", name},
            {"hpPct", round2(ptr->GetHPPercentage())},
            {"combatLog", msg},
            {"action", action}
        });
    }

    // Mobs
    for (const auto& [name, ptr] : Mobs)
    {
        if (!ptr) continue;

        auto it = MobLast.find(name);
        const std::string msg    = (it != MobLast.end()) ? it->second.first  : "";
        const std::string action = (it != MobLast.end()) ? it->second.second : "";

        out["Mobs"].push_back({
            {"mob", name},
            {"hpPct", round2(ptr->GetHPPercentage())},   // if mobs use absolute HP, change to ptr->GetHP() or add GetHPPercentage for mobs too
            {"combatLog", msg},
            {"action", action}
        });
    }

    return out;
}


void CombatLog::MarkDirty() {
  std::lock_guard<std::mutex> lk(CacheMutex);
  Dirty = true;
}

std::string CombatLog::GetJsonCached() {
  std::lock_guard<std::mutex> lk(CacheMutex);
  if (!Dirty) return CachedJson;

  json j = BuildJson();          // your existing function
  CachedJson = j.dump();
  Dirty = false;
  return CachedJson;
}