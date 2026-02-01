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
#include "DaraConfig.h"

enum class EGamePhase
{
    Running,
    GameOverPause
};

inline void DaraLog(const std::string area, const std::string& msg)
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const std::time_t tt = system_clock::to_time_t(now);

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    std::cout << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
              << " ["<<area << "] " << msg << std::endl;
}


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
    bool ApplyDamageToPlayer(const std::string& playerName, float dmg);
    bool ApplyDamageToPlayerLocked(const std::string& playerName, float dmg);
    void RemovePlayer(const std::string& playerName);

    json GetPlayerStateJson(const std::string& playerName) const;

    void AddOrUpdateMob(const std::string& mobName);
    void SpawnMob(const std::string& mobId, int lane, int slot);

    bool ApplyDamageToMob(const std::string& mobName, float dmg, std::string* err);
    void RemoveMob(const std::string& mobName);

    // game state management
    json GetCombatState() const;
    json SerializePlayersLocked() const;
    json SerializeMobsLocked() const;

    json GetUIStateSnapshotJsonLocked() const;
    void SetLane(std::string mobname, int lanenumber, int slotnumber);


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

    void BuildSpawnInfoMsg(std::string mobname, std::string  difficulty, std::string attackType);

    EGamePhase GetPhase();
    bool CheckGameOverLocked(std::string& outReason);
    void ResetGameLocked();

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
    void RegenPlayers();

    json BuildAiRequestSnapshotLocked(uint64_t turnId) const;

    void ApplyAiResults(const json& aiJson,
                        std::vector<std::string>& outTurnLog);

    void ResolveMobs(const json& aiJson,
                     std::vector<std::string>& outTurnLog);
    void ResolveDeadMobs();
    void ResolveSpawnMobs(); //Spawn Mobs
    void ResolveMobAttacks();

    void GetFilledSlotArray();


    bool CheckGameOverLocked(std::string& outReason) const;

    void AppendLogLocked(uint64_t turnId, const std::vector<std::string>& lines);



private:
    const std::string GameId;
    std::string InfoMsg;
    // Game over handling
    EGamePhase Phase = EGamePhase::Running;
    std::string GameOverReason;
    std::chrono::steady_clock::time_point GameOverUntil{};
    std::chrono::milliseconds GameOverPauseDuration{DARA_GAMEOVER_PAUSE}; // 3 seconds
    uint64_t LastGameOverTurnId = 0;

    // ---- guarded by CacheMutex ----
    mutable std::mutex CacheMutex;
    mutable std::condition_variable Cv;

    std::unordered_map<std::string, std::shared_ptr<Combatant>> Players;
    std::unordered_map<std::string, std::shared_ptr<Combatant>> Mobs;

    bool FilledSlotArray[MAXLANES][MAXSLOTS];
    int OpenSlotAmount=0;
    int SpawnedMobsAmount=0;

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
    std::chrono::milliseconds TurnTimeout { DARA_TURN_TIMEOUT };

    // injected AI callback
    AiCallback AiCb;

    // thread control
    std::thread Worker;
    std::atomic<bool> Running { false };
};
