#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <deque>

#include "json.hpp"
#include "combatant.h"

using json = nlohmann::json;

class CombatLog
{
public:
    // Inject references to existing state (no ownership)
    CombatLog(std::unordered_map<std::string, CombatantPtr>& players,
              std::mutex& playersMutex,
              std::unordered_map<std::string, CombatantPtr>& mobs,
              std::mutex& mobsMutex);

    std::string GetJsonCached();   // returns cached JSON string
    void MarkDirty();             // call when something changes
    // Add a log line for a player or mob (thread-safe)
    void PushPlayerLog(const std::string& playerName, const std::string& msg, const std::string& action);
    void PushMobLog(const std::string& mobName, const std::string& msg, const std::string& action);

    // Build the JSON payload for /combatlog (thread-safe)
    json BuildJson() const;

    // Optional: limit memory
    void SetMaxEntriesPerEntity(std::size_t maxEntries);

private:
    mutable std::mutex CacheMutex;
    bool Dirty = true;
    std::string CachedJson;

    std::unordered_map<std::string, CombatantPtr>& Players;
    std::mutex& PlayersMutex;
    std::unordered_map<std::string, CombatantPtr>& Mobs;
    std::mutex& MobsMutex;

    // Store recent messages per entity
    mutable std::mutex LogMutex;
    std::size_t MaxEntriesPerEntity = 1;

    // latest entry per entity (simple)
    std::unordered_map<std::string, std::pair<std::string, std::string>> PlayerLast; // name -> (msg, action)
    std::unordered_map<std::string, std::pair<std::string, std::string>> MobLast;    // name -> (msg, action)
};
