#include "CombatDirector.h"
#include "combatant.h"
#include <algorithm>
#include <iostream>

CombatDirector::CombatDirector(std::string gameId)
    : GameId(std::move(gameId))
{
}

CombatDirector::~CombatDirector()
{
    Stop();
}

void CombatDirector::Start()
{
    bool expected = false;
    if (!Running.compare_exchange_strong(expected, true))
        return; // already running

    Worker = std::thread([this]() { ResolverLoop(); });
}

void CombatDirector::Stop()
{
    bool expected = true;
    if (!Running.compare_exchange_strong(expected, false))
        return; // already stopped

    {
        std::lock_guard<std::mutex> lk(CacheMutex);
        Cv.notify_all();
    }

    if (Worker.joinable())
        Worker.join();
}

void CombatDirector::SetTurnTimeout(std::chrono::milliseconds timeout)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    TurnTimeout = timeout;
    Cv.notify_all();
}

void CombatDirector::SetAiCallback(AiCallback cb)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    AiCb = std::move(cb);
}

uint64_t CombatDirector::GetCurrentTurnId() const
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    return CurrentTurnId;
}

void CombatDirector::AddOrUpdatePlayer(const std::string& playerName)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    if (playerName.empty()) return;

    if (!Players.count(playerName))
        Players.emplace(playerName, std::make_shared<Combatant>(playerName, ECombatantType::Player, MAXHP, MAXENERGY, MAXMANA));
        
    Cv.notify_all();
}

json CombatDirector::GetPlayerStateJson(const std::string& playerName) const
{
    std::shared_ptr<Combatant> p;

    { // nur kurz locken
        std::lock_guard<std::mutex> lk(CacheMutex);
        auto it = Players.find(playerName);
        if (it == Players.end())
            return json{{"error","Unknown player"}};

        p = it->second; // shared_ptr kopieren
    }

    // außerhalb vom CacheMutex lesen
    json out;
    out["hpPct"]      = p->GetHPPercentage();
    out["energyPct"]  = p->GetEnergyPercentage();
    out["manaPct"]    = p->GetManaPercentage();
    out["level"]      = p->GetLevel();
    out["experience"] = p->GetExperience();
    out["gold"]       = p->GetGold();
    return out;
}

bool CombatDirector::ApplyDamageToPlayer(const std::string& playerName, float dmg, std::string* err)
{
    std::shared_ptr<Combatant> p;

    {
        std::lock_guard<std::mutex> lk(CacheMutex);
        auto it = Players.find(playerName);
        if (it == Players.end()) {
            if (err) *err = "Unknown player";
            return false;
        }
        p = it->second;
    }

    p->ApplyDamage(dmg); // außerhalb CacheMutex
    return true;
}

void CombatDirector::RemovePlayer(const std::string& playerName)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    Players.erase(playerName);
    PendingActions.erase(playerName);
    BufferedActions.erase(playerName);
    Cv.notify_all();
}

void CombatDirector::AddOrUpdateMob(const std::string& mobName)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    if (mobName.empty()) return;

    if (!Mobs.count(mobName))
        Mobs.emplace(mobName, std::make_shared<Combatant>(mobName, ECombatantType::Mob, MAXHP, MAXENERGY, MAXMANA));
}

void CombatDirector::RemoveMob(const std::string& mobName)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    Mobs.erase(mobName);
}

/*
Enhancements applied:
- Removed client turnId requirement (server-authoritative turns).
- Actions submitted while Resolving are buffered for the next turn (CurrentTurnId + 1).
- On timeout/early advance: missing players get auto "WAIT".
- After resolving a turn: advance turn and move BufferedActions -> PendingActions for new open turn.
- (Policy) First action wins per turn: if already submitted for that turn/buffer, reject.
  If you prefer "last action wins", replace the rejects with overwrites.
*/
bool CombatDirector::SubmitPlayerAction(const std::string& playerName,
                                       const std::string& actionId,
                                       const std::string& actionTarget,
                                       const std::string& actionMsg,
                                       std::string* outError)
{
    std::lock_guard<std::mutex> lk(CacheMutex);

    if (playerName.empty())
    {
        if (outError) *outError = "Empty playerName not allowed";
        return false;
    }

    if (!Players.count(playerName))
    {
        if (outError) *outError = "Unknown player";
        return false;
    }

    PlayerAction act{playerName, actionId, actionTarget, actionMsg};

    // If current turn is closed/resolving, queue for next turn
    if (Resolving)
    {
        if (BufferedActions.count(playerName))
        {
            if (outError) *outError = "Already submitted for next turn (buffered)";
            return false;
        }

        BufferedActions.emplace(playerName, std::move(act));
        Cv.notify_all();
        return true;
    }

    // Current turn is open
    if (PendingActions.count(playerName))
    {
        if (outError) *outError = "Player already submitted action for this turn";
        return false;
    }

    PendingActions.emplace(playerName, std::move(act));
    Cv.notify_all();
    return true;
}

CombatDirector::json CombatDirector::GetStateSnapshotJson(size_t lastLogLines) const
{
    std::lock_guard<std::mutex> lk(CacheMutex);

    json out;
    out["gameId"] = GameId;
    out["turnId"] = CurrentTurnId;
    out["resolving"] = Resolving;

    // expected players and who submitted (current open turn)
    out["players_expected"] = json::array();
    for (const auto& kv : Players)
        out["players_expected"].push_back(kv.first);

    out["players_submitted"] = json::array();
    for (const auto& kv : PendingActions)
        out["players_submitted"].push_back(kv.first);

    // buffered submissions (queued for next turn)
    out["players_buffered_next_turn"] = json::array();
    for (const auto& kv : BufferedActions)
        out["players_buffered_next_turn"].push_back(kv.first);

    out["players"] = SerializePlayersLocked();
    out["mobs"] = SerializeMobsLocked();

    // last N log lines
    out["log"] = json::array();
    const size_t n = std::min(lastLogLines, Log.size());
    for (size_t i = Log.size() - n; i < Log.size(); ++i)
        out["log"].push_back(Log[i].text);

    return out;
}

CombatDirector::json CombatDirector::SerializePlayersLocked() const
{
    json arr = json::array();
    for (const auto& kv : Players)
    {
        const auto& c = kv.second;
        arr.push_back({
            {"name", c->GetName()},
            {"hp", c->GetHP()},
            {"energy", c->GetEnergy()},
            {"mana", c->GetMana()}
        });
    }
    return arr;
}

CombatDirector::json CombatDirector::SerializeMobsLocked() const
{
    json arr = json::array();
    for (const auto& kv : Mobs)
    {
        const auto& c = kv.second;
        arr.push_back({
            {"name", c->GetName()},
            {"energy", c->GetEnergy()},
            {"mana", c->GetMana()}
        });
    }
    return arr;
}

void CombatDirector::ResolverLoop()
{
    while (Running.load())
    {
        std::vector<PlayerAction> actions;
        uint64_t turnId = 0;

        {
            std::unique_lock<std::mutex> lk(CacheMutex);

            // If no players, just idle
            if (Players.empty())
            {
                Cv.wait_for(lk, std::chrono::milliseconds(200));
                continue;
            }

            // If we're already resolving (shouldn't happen often), wait a bit
            if (Resolving)
            {
                Cv.wait_for(lk, std::chrono::milliseconds(50));
                continue;
            }

            turnId = CurrentTurnId;
            auto deadline = std::chrono::steady_clock::now() + TurnTimeout;

            // wait until all players submitted or deadline
            WaitForTurnBarrier(lk, turnId, deadline);

            // Fill missing players with WAIT so turn always completes deterministically
            for (const auto& kv : Players)
            {
                const std::string& name = kv.first;
                if (!PendingActions.count(name))
                {
                    PendingActions.emplace(name, PlayerAction{name, "actionWait", "", ""});
                }
            }

            // begin resolving; move pending actions out
            Resolving = true;

            actions.reserve(PendingActions.size());
            for (auto& kv : PendingActions)
                actions.push_back(std::move(kv.second));
            PendingActions.clear();

            // stable order for deterministic resolution
            std::sort(actions.begin(), actions.end(),
                      [](const PlayerAction& a, const PlayerAction& b)
                      {
                          return a.playerName < b.playerName;
                      });
        }

        // Step B: resolve players (no lock)
        std::vector<std::string> turnLog;
        turnLog.push_back("=== TURN " + std::to_string(turnId) + " ===");
        ResolvePlayers(actions, turnLog);

        // Step C: AI (no lock). Build snapshot under lock, then call AI unlocked.
        json aiRequest;
        {
            std::lock_guard<std::mutex> lk(CacheMutex);
            aiRequest = BuildAiRequestSnapshotLocked(turnId);
        }

        json aiResponse;
        {
            AiCallback cbCopy;
            {
                std::lock_guard<std::mutex> lk(CacheMutex);
                cbCopy = AiCb;
            }

            if (cbCopy)
            {
                try { aiResponse = cbCopy(aiRequest); }
                catch (const std::exception& e)
                {
                    turnLog.push_back(std::string("AI call failed: ") + e.what());
                }
            }
            else
            {
                aiResponse = json::object();
            }
        }

        // Step D/E/F/G: apply AI + resolve mobs + game over + advance turn (lock for applying)
        {
            std::lock_guard<std::mutex> lk(CacheMutex);

            ApplyAiResults(aiResponse, turnLog);
            ResolveMobs(aiResponse, turnLog);

            std::string reason;
            if (CheckGameOverLocked(reason))
            {
                turnLog.push_back("GAME OVER: " + reason);
                // you might set a flag here to stop resolving further turns
            }

            AppendLogLocked(turnId, turnLog);

            // advance turn
            CurrentTurnId++;
            Resolving = false;

            // Move buffered (queued) actions into the new open turn
            PendingActions = std::move(BufferedActions);
            BufferedActions.clear();

            Cv.notify_all();
        }
    }
}

bool CombatDirector::WaitForTurnBarrier(std::unique_lock<std::mutex>& lk,
                                       uint64_t turnId,
                                       std::chrono::steady_clock::time_point deadline)
{
    (void)turnId;

    auto allPlayersSubmitted = [&]() -> bool
    {
        for (const auto& kv : Players)
        {
            const std::string& name = kv.first;
            if (!PendingActions.count(name))
                return false;
        }
        return !Players.empty();
    };

    while (Running.load())
    {
        if (allPlayersSubmitted())
            return true;

        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        Cv.wait_until(lk, deadline);
    }
    return false;
}

void CombatDirector::ResolvePlayers(const std::vector<PlayerAction>& actions,
                                   std::vector<std::string>& outTurnLog)
{
    for (const auto& a : actions)
    {
        outTurnLog.push_back(a.playerName + " does: " + a.actionId + " (" + a.actionMsg + ")");
    }
}

CombatDirector::json CombatDirector::BuildAiRequestSnapshotLocked(uint64_t turnId) const
{
    json req;
    req["gameId"] = GameId;
    req["turnId"] = turnId;

    req["players"] = SerializePlayersLocked();
    req["mobs"] = SerializeMobsLocked();

    const size_t n = std::min<size_t>(20, Log.size());
    req["recent_log"] = json::array();
    for (size_t i = Log.size() - n; i < Log.size(); ++i)
        req["recent_log"].push_back(Log[i].text);

    return req;
}

void CombatDirector::ApplyAiResults(const json& aiJson,
                                   std::vector<std::string>& outTurnLog)
{
    if (aiJson.is_null() || aiJson.empty())
        return;

    if (aiJson.contains("narrative"))
    {
        outTurnLog.push_back("AI: " + aiJson["narrative"].get<std::string>());
    }
}

void CombatDirector::ResolveMobs(const json& aiJson,
                                std::vector<std::string>& outTurnLog)
{
    (void)aiJson;

    if (!Mobs.empty())
        outTurnLog.push_back("Mobs act (placeholder).");
}

bool CombatDirector::CheckGameOverLocked(std::string& outReason) const
{
    if (Players.empty())
        return false;

    bool allDead = true;
    for (const auto& kv : Players)
    {
        if (kv.second->GetHP() > 0)
        {
            allDead = false;
            break;
        }
    }

    if (allDead)
    {
        outReason = "All players are dead";
        return true;
    }
    return false;
}

void CombatDirector::AppendLogLocked(uint64_t turnId, const std::vector<std::string>& lines)
{
    const auto now = std::chrono::system_clock::now();
    for (const auto& line : lines)
    {
        Log.push_back(LogEntry{turnId, line, now});
    }

    constexpr size_t kMaxLog = 2000;
    if (Log.size() > kMaxLog)
    {
        Log.erase(Log.begin(), Log.begin() + (Log.size() - kMaxLog));
    }
}
