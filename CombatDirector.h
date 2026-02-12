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
#include "character.h"

enum class EGamePhase
{
    WaveCompleted,
    Running,
    GameOverPause
};
// Example reward payload (expand as you like)
struct MobRewards
{
    int xpMin = 1, xpMax = 5;
    float creditsChance= 0.10f;
    int creditsMin = 1, creditsMax = 10;
    float lootChance = 0.10f; // 10%
    // You can add loot tables etc.
    int MinCredits= 0;
    int MobAddsXP= 0;
};




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
    void AddOrUpdatePlayer(const std::string& playerName, Character selectedCharacter);
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

    json GetUIStateSnapshotJsonLocked(
        const std::string playerName, const std::string characterId, const std::string characterName) const;
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

    // loot
    std::vector<std::shared_ptr<Combatant>> SnapshotPlayersLocked() const;
    void MaybeGiveLoot(std::mt19937& rng, Combatant& player, const std::string& mobName);
    MobRewards GetMobRewards(const Combatant& mob) const;
    void RewardPlayersForMobDeath(
        const std::vector<std::shared_ptr<Combatant>>& players,
        const Combatant& mob,
        const std::string& mobName);
    // end loot

    EGamePhase GetPhase();
    bool CheckGameOverLocked(std::string& outReason);
    void ResetGameLocked();

    bool HasPlayer(const std::string& playerName) const; 

private:
    int Wave=1;
    int WaveMobsLeft=10;
    int MobToSpawnInWave= 5;
    int WaveWaitTurns=5;

    void NewWave();
    void ResetWave();

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
    void RegenMobs();

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

    void KickInactivePlayersLocked();



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
