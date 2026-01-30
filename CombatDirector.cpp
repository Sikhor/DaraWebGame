#include "CombatDirector.h"
#include "combatant.h"
#include <algorithm>
#include <iostream>
#include "DaraConfig.h"
#include "uistate.h"

int RandSlot()
{
    static thread_local std::mt19937 rng{ std::random_device{}() };
    static std::uniform_int_distribution<int> dist(0, MAXSLOTS-1);
    return dist(rng);
}

CombatDirector::CombatDirector(std::string gameId)
    : GameId(std::move(gameId))
{
    for (int l = 0; l < MAXLANES; ++l)
    for (int s = 0; s < MAXSLOTS; ++s)
        FilledSlotArray[l][s] = false;
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
    out["hpPct"]      = p->GetHPPct();
    out["energyPct"]  = p->GetEnergyPct();
    out["manaPct"]    = p->GetManaPct();
    out["level"]      = p->GetLevel();
    out["experience"] = p->GetExperience();
    out["gold"]       = p->GetGold();
    return out;
}

json CombatDirector::GetCombatState() const
{
    std::lock_guard<std::mutex> lk(CacheMutex);

    json out;
    out["Players"] = SerializePlayersLocked();
    out["Mobs"]    = SerializeMobsLocked();

    return out;
}

json CombatDirector::SerializePlayersLocked() const
{
    json arr = json::array();

    for (const auto& [name, player] : Players)
    {
        json p;
        p["player"]    = player->GetName();
        p["hpPct"]     = player->GetHPPct();              // int 0–100
        p["combatLog"] = "";   // string
        p["action"]    = "";   // "attack", "defend", ...

        arr.push_back(std::move(p));
    }

    return arr;
}
json CombatDirector::SerializeMobsLocked() const
{
    json arr = json::array();

    for (const auto& [name, mob] : Mobs)
    {
        json m;
        m["mob"]    = mob->GetName();
        m["hpPct"]     = mob->GetHPPct();              // int 0–100
        m["combatLog"] = "";   // string
        m["action"]    = "";   // "attack", "defend", ...
        arr.push_back(std::move(m));
    }

    return arr;
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
    if (mobName.empty()) return;

    if (!Mobs.count(mobName))
        Mobs.emplace(mobName, std::make_shared<Combatant>(mobName, ECombatantType::Mob, MAXHP, MAXENERGY, MAXMANA));
}

void CombatDirector::SetLane(std::string mobName, int lanenumber, int slotnumber)
{
    std::shared_ptr<Combatant> m;
    {
        auto it = Mobs.find(mobName);
        if (it == Mobs.end()) {
            std::cerr << "CombatDirector::SetLane: Unknown mob"<< std::endl;    
            return;
        }
        m = it->second;
    }

    m->SetLane(lanenumber, slotnumber) ; // außerhalb CacheMutex
  
}

bool CombatDirector::ApplyDamageToMob(const std::string& mobName, float dmg, std::string* err)
{
    std::shared_ptr<Combatant> m;

    {
        std::lock_guard<std::mutex> lk(CacheMutex);
        auto it = Mobs.find(mobName);
        if (it == Mobs.end()) {
            if (err) *err = "Unknown mob";
            return false;
        }
        m = it->second;
    }

    m->ApplyDamage(dmg); // außerhalb CacheMutex
    return true;
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

CombatDirector::json CombatDirector::SerializePlayersAllLocked() const
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

CombatDirector::json CombatDirector::SerializeMobsAllLocked() const
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

void CombatDirector::ResolveDeadMobs()
{
      for (auto it = Mobs.begin(); it != Mobs.end(); )
    {
        const auto& mobName = it->first;
        const auto& mob = it->second;

        if(mob->GetAvatarId()=="Dead"){
            it = Mobs.erase(it);     // <- korrekt: erase gibt nächsten Iterator zurück
            continue;
        }
        if (!mob || !mob->IsAlive())
        {
            mob->SetAvatarId("Dead");
            ++it;
            continue;
        }
        ++it;
    }
}

void CombatDirector::GetFilledSlotArray()
{
    // alles auf false
    for (int l = 0; l < MAXLANES; ++l)
        for (int s = 0; s < MAXSLOTS; ++s)
            FilledSlotArray[l][s] = false;
    OpenSlotAmount=MAXLANES*MAXSLOTS;
    SpawnedMobsAmount=0;

    // check which is open
    for (const auto& [name, mob] : Mobs)
    {
        if (!mob || mob->GetHP() <= 0){
            continue;
        }

        int lane = mob->GetLane();
        int slot = mob->GetSlot();

        if (lane >= 0 && lane < MAXLANES &&
            slot >= 0 && slot < MAXSLOTS)
        {
            FilledSlotArray[lane][slot] = true; // belegt
            SpawnedMobsAmount++;
            OpenSlotAmount--;
        }
    }
    std::cout << "OpenSlots: "<< OpenSlotAmount << " FilledSlotAmount: "<< SpawnedMobsAmount<< std::endl;

}
void CombatDirector::SpawnMob(const std::string& mobName, ECombatantType type, float hp, float energy, float mana, std::string mobClass, int lane, int slot)
{
    if (mobName.empty()) return;

    if (!Mobs.count(mobName) && SpawnedMobsAmount<=DARA_MAX_MOBS){
        Mobs.emplace(mobName, std::make_shared<Combatant>(mobName, ECombatantType::Mob, hp, energy, mana, mobClass, lane, slot));
        if(DARA_DEBUG_SPAWNS) std::cout <<"Spawn Mob"<< mobName<<" Class"<< mobClass<< " Slot:" << slot <<std::endl;

    }

}

void CombatDirector::ResolveSpawnMobs()
{
    static int MobNumber=0;
    std::string MobClass="Spider";
    int slot= RandSlot();
    
    GetFilledSlotArray();

    if(!FilledSlotArray[0][slot]){
        std::string MobName= MobClass+std::to_string(MobNumber);
        MobNumber++;
        SpawnMob(MobName, ECombatantType::Mob, 20.f, 20.f,20.f, MobClass, 0, slot);

    }
}

void CombatDirector::ResolveMobAttacks()
{
    for (auto it = Mobs.begin(); it != Mobs.end(); ++it)
    {
        const auto& mob = it->second;
        if (!mob) continue;

        int nextLane = mob->GetLane() + 1;
        int slot = mob->GetSlot();

        if (nextLane < MAXLANES && !FilledSlotArray[nextLane][slot] && mob->ShouldMove())
        {
            // slot in current lane frei machen
            FilledSlotArray[mob->GetLane()][slot] = false;

            mob->SetLane(nextLane, slot);

            // slot in next lane belegen
            FilledSlotArray[nextLane][slot] = true;
        }
    }
}



void CombatDirector::ResolveMobs(const json& aiJson,
                                std::vector<std::string>& outTurnLog)
{
    (void)aiJson;
    // 1) Dead mobs entfernen (LOCK ist schon gehalten!)
    ResolveDeadMobs();
    // 2) Spawn Mobs
    ResolveSpawnMobs();
    // 3) Mobs move and attack 
    ResolveMobAttacks();

    if (!Mobs.empty())
        outTurnLog.push_back("Mobs act (placeholder).");

    // Build a list of alive players (shared_ptrs)
    std::vector<std::shared_ptr<Combatant>> alivePlayers;
    alivePlayers.reserve(Players.size());
    for (const auto& [playerName, p] : Players)
    {
        if (p && p->GetHP() > 0)
            alivePlayers.push_back(p);
    }

    if (alivePlayers.empty())
        return;

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> pick(0, alivePlayers.size() - 1);

    constexpr float kDamage = 1.0f;

    for (auto& [mobName, mob] : Mobs)
    {
        if (!mob) continue;
        // Optional: skip dead mobs
        // if (mob->GetHP() <= 0) continue;

        auto target = alivePlayers[pick(rng)];
        if (!target) continue;

        // Apply damage directly (NO second CacheMutex lock!)
        target->ApplyDamage(kDamage);
        mob->AttackMelee(target->GetName());    

        // Log globally (turn log)
        outTurnLog.push_back(
            mobName + " attacks " + target->GetName() +
            " for " + std::to_string((int)kDamage) + " dmg."
        );

        if(DARA_DEBUG_COMBATLOG) {
            std::cout << "CombatLog: " << mobName << " attacks " << target->GetName() <<
                " for " << (int)kDamage << " dmg." <<std::endl;
        }
        // Optional per-entity log fields if you have them:
        // mob->SetLastAction("attack");
        // mob->SetCombatLog("Attacked " + target->GetName());
        // target->SetCombatLog("Hit by " + mobName);
    }
    
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


CombatDirector::json CombatDirector::GetLogTailJson(size_t lastN) const
{
    std::lock_guard<std::mutex> lk(CacheMutex);

    json out;
    out["gameId"] = GameId;
    out["turnId"] = CurrentTurnId;

    const size_t total = Log.size();
    const size_t n = std::min(lastN, total);

    json arr = json::array();
    for (size_t i = total - n; i < total; ++i)
    {
        const auto& e = Log[i];
        arr.push_back({
            {"i", i},
            {"turnId", e.turnId},
            {"text", e.text}
        });
    }

    out["total"] = total;
    out["entries"] = std::move(arr);
    return out;
}


json CombatDirector::GetUIStateSnapshotJsonLocked() const
{
        UIState ui;

    ui.SetTurn(CurrentTurnId);
    ui.SetSlots(5); // your encounter grid width

    // Convert Players + Mobs → JSON
    nlohmann::json uiJson = ui.ToJson(Players, Mobs);
    return uiJson;
}