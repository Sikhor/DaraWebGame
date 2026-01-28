#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

#include "json.hpp"
#include "combatant.h"

class CombatDirector
{
public:
    using json = nlohmann::json;

    // AI callback: take request json -> return response json
    using AiCallback = std::function<json(const json&)>;

    struct PlayerAction
    {
        std::string playerName;
        std::string actionId;       // e.g. "actionAttack"
        std::string actionTarget;   // e.g. "Polta"
        std::string actionMsg;      // e.g. "attack Polta"
    };

    explicit CombatDirector(std::string gameId);
    ~CombatDirector();

    CombatDirector(const CombatDirector&) = delete;
    CombatDirector& operator=(const CombatDirector&) = delete;

    // Start/stop resolver thread
    void Start();
    void Stop();

    // Player/mob management (call when joining/leaving/spawning)
    void AddOrUpdatePlayer(const std::string& playerName);
    bool ApplyDamageToPlayer(const std::string& playerName, float dmg, std::string* err);
    void RemovePlayer(const std::string& playerName);

    json GetPlayerStateJson(const std::string& playerName) const;

    void AddOrUpdateMob(const std::string& mobName);
    bool ApplyDamageToMob(const std::string& mobName, float dmg, std::string* err);
    void RemoveMob(const std::string& mobName);

    // game state management
    json GetCombatState() const;
    json SerializePlayersLocked() const;
    json SerializeMobsLocked() const;
    // Snapshot helpers for logging
    json SerializePlayersAllLocked() const;
    json SerializeMobsAllLocked() const;

    // Submit action for the currently-open turn (server authoritative).
    // If the turn is resolving, the action is queued for the next turn.
    // Returns false if rejected (unknown player, duplicate submit for that bucket, etc.)
    bool SubmitPlayerAction(const std::string& playerName,
                            const std::string& actionId,
                            const std::string& actionTarget,
                            const std::string& actionMsg,
                            std::string* outError = nullptr);

    // Read-only snapshot for GET /state
    json GetStateSnapshotJson(size_t lastLogLines = 30) const;
    // Convenience: last N lines (you already do something similar in GetStateSnapshotJson)
    json GetLogTailJson(size_t lastN) const;

    // Configuration
    void SetTurnTimeout(std::chrono::milliseconds timeout);
    void SetAiCallback(AiCallback cb);

    // Optional: get current turn id (for clients / debug)
    uint64_t GetCurrentTurnId() const;

private:
    // Thread loop
    void ResolverLoop();

    // Barrier: wait until all actions received or deadline
    bool WaitForTurnBarrier(std::unique_lock<std::mutex>& lk,
                            uint64_t turnId,
                            std::chrono::steady_clock::time_point deadline);

    // Turn pipeline
    void ResolvePlayers(const std::vector<PlayerAction>& actions,
                        std::vector<std::string>& outTurnLog);

    json BuildAiRequestSnapshotLocked(uint64_t turnId) const;

    void ApplyAiResults(const json& aiJson,
                        std::vector<std::string>& outTurnLog);

    void ResolveMobs(const json& aiJson,
                     std::vector<std::string>& outTurnLog);

    bool CheckGameOverLocked(std::string& outReason) const;

    void AppendLogLocked(uint64_t turnId, const std::vector<std::string>& lines);


private:
    const std::string GameId;

    // ---- guarded by CacheMutex ----
    mutable std::mutex CacheMutex;
    mutable std::condition_variable Cv;

    std::unordered_map<std::string, std::shared_ptr<Combatant>> Players;
    std::unordered_map<std::string, std::shared_ptr<Combatant>> Mobs;

    struct LogEntry
    {
        uint64_t turnId;
        std::string text;
        std::chrono::system_clock::time_point time;
    };
    std::vector<LogEntry> Log;

    // Turn state
    uint64_t CurrentTurnId = 1;
    bool Resolving = false;

    // Actions for the currently-open turn
    std::unordered_map<std::string, PlayerAction> PendingActions;   // playerName -> action

    // Actions submitted while resolving (queued for next turn)
    std::unordered_map<std::string, PlayerAction> BufferedActions;  // playerName -> action

    // config
    std::chrono::milliseconds TurnTimeout { 60000 };

    // injected AI callback
    AiCallback AiCb;

    // thread control
    std::thread Worker;
    std::atomic<bool> Running { false };
};
