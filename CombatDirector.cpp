#include "CombatDirector.h"
#include "combatant.h"
#include <algorithm>
#include <iostream>
#include "uistate.h"
#include "MobTemplateStore.h"
#include "CharacterRepository.h"
#include "CharacterDbWorker.h"

extern MobTemplateStore g_mobTemplates;
extern CharacterDbWorker g_dbWorker;

static int RandInt(std::mt19937& rng, int a, int b)
{
    std::uniform_int_distribution<int> dist(a, b);
    return dist(rng);
}
static float Rand01(std::mt19937& rng)
{
    std::uniform_real_distribution<float> dist(0.f, 1.f);
    return dist(rng);
}


int RandSlot()
{
    static thread_local std::mt19937 rng{ std::random_device{}() };
    static std::uniform_int_distribution<int> dist(0, MAXSLOTS-1);
    return dist(rng);
}
bool ShallMobSpawn(int turn)
{
    static thread_local std::mt19937 rng{ std::random_device{}() };
    static std::uniform_int_distribution<int> dist(0, 1000);
    return dist(rng)>turn;
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

    const bool wasEmpty = Players.empty();

    if (!Players.count(playerName))
        Players.emplace(playerName, std::make_shared<Combatant>(playerName, ECombatantType::Player, STAT_BASE_MAX_HP, STAT_BASE_MAX_ENERGY, STAT_BASE_MAX_MANA));
    DaraLog("LOGIN", "Player "+ playerName+ " logged in");

    // If this is the first player coming back, ensure we are not stuck in WaveCompleted
    if (wasEmpty)
    {
        DaraLog("PHASE", "Battlefield was empty why should we set WaveCompleted... we do NOT!");
    }

    Cv.notify_all();
}

void CombatDirector::AddOrUpdatePlayer(const std::string& playerName, Character selectedCharacter)
{
    std::lock_guard<std::mutex> lock(CacheMutex);

    // Players is a map/unordered_map: key -> shared_ptr<Combatant>
    auto it = std::find_if(Players.begin(), Players.end(),
        [&](const auto& kv)   // kv is: pair<const string, shared_ptr<Combatant>>
        {
            const auto& p = kv.second;
            if (!p) return false;

            const bool sameChar =
                (!selectedCharacter.characterId.empty() &&
                 p->GetId() == selectedCharacter.characterId);

            const bool sameName = (p->GetName() == playerName);

            return sameChar || sameName;
        });

    const bool exists = (it != Players.end());

    if (!exists)
    {
        auto p = std::make_shared<Combatant>();
        p->InitPlayerType();

        p->InitName(playerName);
        p->InitId(selectedCharacter.characterId);

        p->InitLevel(selectedCharacter.level);
        p->InitXP(selectedCharacter.xp);
        p->InitCredits(selectedCharacter.credits);
        p->InitPotions(selectedCharacter.potions);
        p->SetAvatarId(selectedCharacter.avatar);

        // Choose what key to store under:
        // Option A: key by playerName (display name)
        Players[playerName] = p;

        // Option B (recommended): key by characterId or userId (stable)
        // Players[selectedCharacter.characterId] = p;

        return;
    }

    // Update existing
    auto& p = it->second;
    p->InitName(playerName);
    p->InitId(selectedCharacter.characterId);
    p->InitLevel(selectedCharacter.level);
    p->InitXP(selectedCharacter.xp);
    p->InitCredits(selectedCharacter.credits);
    p->InitPotions(selectedCharacter.potions);
}



json CombatDirector::GetPlayerStateJson(const std::string& playerName) const
{
    std::shared_ptr<Combatant> p;
    {
        std::lock_guard<std::mutex> lk(CacheMutex);
        auto it = Players.find(playerName);
        if (it == Players.end())
            return json{{"error","Unknown player"}};
        p = it->second;
    }

    // außerhalb vom CacheMutex lesen
    json out;
    out["hpPct"]      = p->GetHPPct();
    out["energyPct"]  = p->GetEnergyPct();
    out["manaPct"]    = p->GetManaPct();
    out["level"]      = p->GetLevel();
    out["xp"] = p->GetXP();
    out["credits"]       = p->GetCredits();
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
        p["hp"]    = player->GetHP();
        p["hpMax"] = player->GetMaxHP();
        p["en"]    = player->GetEnergy();
        p["enMax"] = player->GetMaxEnergy();
        p["mn"]    = player->GetMana();
        p["mnMax"] = player->GetMaxMana();

        p["combatLog"] = "";   // string
        p["action"]    = "";   // "attack", "defend", ...
       
        /*
        std::cout << "SER " << name
          << " HP=" << player->GetHP()
          << " MaxHP=" << player->GetMaxHP()
          << " ptr=" << player.get()
          << "\n";
        */  

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
        m["hp"]     = mob->GetHP();              
        m["hpMax"]     = mob->GetMaxHP();              
        m["combatLog"] = "";   // string
        m["action"]    = "";   // "attack", "defend", ...
        arr.push_back(std::move(m));
    }

    return arr;
}


bool CombatDirector::ApplyDamageToPlayer(const std::string& playerName, float dmg)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    return ApplyDamageToPlayerLocked(playerName, dmg);
}

bool CombatDirector::ApplyDamageToPlayerLocked(const std::string& playerName, float dmg)
{
    auto it = Players.find(playerName);
    if (it == Players.end()) {
        DaraLog("ERROR", "ApplyDamageToPlayer Unknown player"+playerName);
        return false;
    }else{
        std::shared_ptr<Combatant> p=it->second;
        p->ApplyDamage(dmg);
    }

    if(DARA_DEBUG_MOBCOMBAT) DaraLog("COMBAT", "ApplyDamageToPlayer "+playerName+" "+std::to_string(dmg));
    return true;
}



void CombatDirector::RemovePlayer(const std::string& playerName)
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    Players.erase(playerName);
    PendingActions.erase(playerName);
    BufferedActions.erase(playerName);
    if (Players.empty())
    {
        DaraLog("GAMESTATE", "All players logged out → resetting to Wave 0");
        ResetWave();
    }

    Cv.notify_all();
}

void CombatDirector::ResetWave()
{  
        // Clear combat state
        Mobs.clear();
        PendingActions.clear();
        BufferedActions.clear();
        Log.clear();

        // Reset wave + progression
        Wave = 1;
        MobToSpawnInWave = 5;
        WaveWaitTurns = 5;

        // Reset turn state
        CurrentTurnId = 0;
        Resolving = false;

        // Reset phase
        Phase = EGamePhase::Running;
        GameOverReason.clear();

        // Optional: clear info message
        InfoMsg.clear();
}

EGamePhase CombatDirector::GetPhase()
{
    std::lock_guard<std::mutex> lk(CacheMutex);
    return Phase;
};

void CombatDirector::SetLane(std::string mobName, int lanenumber, int slotnumber)
{
    std::shared_ptr<Combatant> m;
    {
        std::lock_guard<std::mutex> lk(CacheMutex);
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
    std::lock_guard<std::mutex> lk(CacheMutex);
    std::shared_ptr<Combatant> m;

    auto it = Mobs.find(mobName);
    if (it == Mobs.end()) {
        if (err) *err = "Unknown mob";
        return false;
    }
    m = it->second;

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
            return true; // because we only send error to player if there is a real problem
        }

        BufferedActions.emplace(playerName, std::move(act));
        Cv.notify_all();
        return true;
    }

    // Current turn is open
    if (PendingActions.count(playerName))
    {
        if (outError) *outError = "Player already submitted action for this turn";
        return true;  // because we only send error to player if there is a real problem
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

    // Game Over handling
    if (Phase==EGamePhase::WaveCompleted){
        out["phase"] = "wavecompleted"    ;
    }else{
        out["phase"] = (Phase == EGamePhase::Running) ? "running" : "gameover";
    }
    out["gameOverReason"] = GameOverReason;

    if (Phase == EGamePhase::GameOverPause)
    {
        auto now = std::chrono::steady_clock::now();
        auto msLeft = (GameOverUntil > now)
            ? std::chrono::duration_cast<std::chrono::milliseconds>(GameOverUntil - now).count()
            : 0;
        out["restartInMs"] = msLeft;
    }
    if (Phase == EGamePhase::WaveCompleted)
    {
        out["restartInMs"] = WaveWaitTurns;
    }
    else
    {
        out["restartInMs"] = 0;
    }
    // last N log lines
    out["log"] = json::array();
    const size_t n = std::min(lastLogLines, Log.size());
    for (size_t i = Log.size() - n; i < Log.size(); ++i)
        out["log"].push_back(Log[i].text);

    return out;
}

void CombatDirector::RegenPlayers()
{
    for (auto& [name, player] : Players)
    {
        if (player && player->IsAlive())
            player->RegenTurn();
    }
}

void CombatDirector::RegenMobs()
{
    for (auto& [name, mob] : Mobs)
    {
        if (mob && mob->IsAlive())
            mob->RegenTurnMob();
    }
}



void CombatDirector::ResolverLoop()
{
    static int dbUpdateCounter=0;
    dbUpdateCounter++;
    if(dbUpdateCounter>10){
        CommitCacheToDB();
        dbUpdateCounter=0;
    }

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

            // Step B: resolve players (no lock) initially... but maybe thats the bug ?

            std::vector<std::string> turnLog;
            turnLog.push_back("=== TURN " + std::to_string(turnId) + " ===");
        {
            std::unique_lock<std::mutex> lk(CacheMutex);

            ResolvePlayers(actions, turnLog);
            RegenPlayers();
            RegenMobs();
        }

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
                // Show a friendly lose message in log (UI will read it)
                turnLog.push_back("YOU LOST! " + reason);
                turnLog.push_back("Restarting in " + std::to_string((int)GameOverPauseDuration.count()/1000) + "s...");
                DaraLog("GAMESTATE", "Game Over");
            }

            // write logs
            AppendLogLocked(turnId, turnLog);

            // advance turn
            CurrentTurnId++;
            Resolving = false;
            PendingActions = std::move(BufferedActions);
            BufferedActions.clear();

            // If we're in game-over pause, do NOT continue instantly.
            // We'll idle until the deadline and then reset.
            Cv.notify_all();
        }
        // After finishing the turn, if game over: sleep/wait until restart time, then reset
        {
            std::unique_lock<std::mutex> lk(CacheMutex);
            if (Phase == EGamePhase::GameOverPause)
            {
                auto until = GameOverUntil;
                lk.unlock();

                // wait without holding CacheMutex
                std::this_thread::sleep_until(until);

                lk.lock();
                // Check again (in case something changed)
                if (Phase == EGamePhase::GameOverPause && std::chrono::steady_clock::now() >= GameOverUntil)
                {
                    ResetGameLocked();
                    // optional: add a log line that a new run started
                    Log.push_back(LogEntry{0, "=== NEW RUN STARTED ===", std::chrono::system_clock::now()});
                }
            }
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
    //DaraLog("RESOLVE", "Started resolve Players with actions in queue: "+std::to_string(actions.size()));
    float dmg=0.f;
    std::string logMsg;
    for (const auto& a : actions)
    {
        //DaraLog("DEBUG", a.playerName + " does: " + a.actionId + " on" + a.actionTarget+ " (" + a.actionMsg + ")");
        outTurnLog.push_back(a.playerName + " does: " + a.actionId + " (" + a.actionMsg + ")");
            std::string logMsg;

        if(a.actionId=="attack" || a.actionId=="fireball"|| a.actionId=="shoot"|| a.actionId=="mezmerize"){
            std::shared_ptr<Combatant> target;
            std::shared_ptr<Combatant> player;
            auto pit=Players.find(a.playerName);
            auto tit=Mobs.find(a.actionTarget);           

            if(pit!=Players.end() && tit!=Players.end()){
                player= pit->second;
                target= tit->second;
                if(a.actionId=="attack")dmg= player->AttackMelee(target);
                if(a.actionId=="fireball")dmg= player->AttackFireball(target);
                if(a.actionId=="shoot")dmg= player->AttackShoot(target);
                if(a.actionId=="mezmerize")dmg= player->AttackMezz(target);
                logMsg= "Success "+std::to_string(dmg);                
            }else{
                logMsg="Could not find player or target";
            }

            if(logMsg.empty()==false){
                DaraLog("COMBAT", a.playerName + " " + a.actionId +" "+ a.actionTarget+" Result: " + logMsg);
            }
        }
        if(a.actionId=="heal" ||a.actionId=="revive" ){
            std::shared_ptr<Combatant> target;
            std::shared_ptr<Combatant> player;
            auto pit=Players.find(a.playerName);
            auto tit=Players.find(a.actionTarget);           

            if(pit!=Players.end() && tit!=Players.end()){
                player= pit->second;
                target= tit->second;
                if(a.actionId=="heal") player->Heal(target);
                if(a.actionId=="revive") target->Revive();

                logMsg= "Success";                
            }else{
                logMsg="Could not find player or target";
            }

            if(logMsg.empty()==false){
                DaraLog("COMBAT", a.playerName + " " + a.actionId +" "+ a.actionTarget+": " + logMsg);
            }
        }
        if(a.actionId=="defense" ||a.actionId=="usepotion" ){
            std::shared_ptr<Combatant> player;
            auto pit=Players.find(a.playerName);

            if(pit!=Players.end()){
                player= pit->second;
                if(a.actionId=="defense") player->BuffDefense();
                if(a.actionId=="usepotion") player->UsePotion();

                logMsg= "Success";                
            }else{
                logMsg="Could not find player or target";
            }

            if(logMsg.empty()==false){
                DaraLog("COMBAT", a.playerName + " " + a.actionId +" "+ a.actionTarget+": " + logMsg);
            }
        }
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


// Snapshot players ONCE.
std::vector<std::shared_ptr<Combatant>> CombatDirector::SnapshotPlayersLocked() const
{
    std::vector<std::shared_ptr<Combatant>> out;
    out.reserve(Players.size());
    for (auto const& [name, p] : Players)
        if (p) out.push_back(p);
    return out;
}

// Your “loot roll” placeholder
void CombatDirector::MaybeGiveLoot(std::mt19937& rng, Combatant& player, const std::string& mobName)
{
    // Example: 1 random “token”
    // Replace with your loot table logic
    (void)mobName;
    (void)rng;

    player.AddPotion(1);
    // player.AddItem("SomeLootId", 1);
}

MobRewards CombatDirector::GetMobRewards(const Combatant& mob) const
{
    MobRewards r;
    (void) mob;
    // Tune by difficulty/level/etc:
    // if (mob.GetDifficulty() == Boss) { ... }
    return r;
}

void CombatDirector::RewardPlayersForMobDeath(
    const std::vector<std::shared_ptr<Combatant>>& players,
    const Combatant& mob,
    const std::string& mobName)
{
    // Thread-local RNG
    static thread_local std::mt19937 rng{ std::random_device{}() };

    const MobRewards r = GetMobRewards(mob);

    for (auto const& pptr : players)
    {
        if (!pptr) continue;
        Combatant& p = *pptr;

        // If you have “online / logged in” markers, check them here.
        // if (!p.IsOnline()) continue;

        const int xp = RandInt(rng, r.xpMin, r.xpMax);
        const int credits = RandInt(rng, r.creditsMin, r.creditsMax);

        int CurrentLevel= p.GetLevel();
        int CurrentXP= p.GetXP();
        int CurrentPotions= p.GetPotionAmount();
        int CurrentCredits= p.GetCredits();

        if(p.GetDifficulty()=="Normal"){
            p.AddXP(xp);
        }else{
            p.AddXP(xp+10);
        }
        if (Rand01(rng) < r.creditsChance)
            p.AddCredits(credits);

        if (Rand01(rng) < r.lootChance)
            MaybeGiveLoot(rng, p, mobName);
        // only save if something changed or XP  has changed by 20 %
        if (p.GetLevel()>CurrentLevel || p.GetXP()>CurrentXP*1.2 || p.GetPotionAmount()>CurrentPotions || p.GetCredits()>CurrentCredits){
            g_dbWorker.RequestSaveCharacter(p.GetId(), p.GetLevel(), p.GetXP(), p.GetCredits(), p.GetPotionAmount(), Wave);
        }

    }
}

void CombatDirector::ResolveDeadMobs()
{
    bool deleteAll = false;

    if (Players.empty())
    {
        deleteAll = true;
        DaraLog("INFO", "Players map is empty");
    }

    // 1) collect “just died” mobs
    std::vector<std::pair<std::string, std::shared_ptr<Combatant>>> justDied;
    justDied.reserve(8);

    for (auto it = Mobs.begin(); it != Mobs.end(); )
    {
        const std::string mobName = it->first;
        std::shared_ptr<Combatant> mob = it->second;

        if (!mob)
        {
            it = Mobs.erase(it);
            continue;
        }

        // remove already-marked corpses or deleteAll
        if (deleteAll || mob->GetAvatarId() == "Dead")
        {
            it = Mobs.erase(it);
            continue;
        }

        // newly dead this turn
        if (!mob->IsAlive())
        {
            mob->SetAvatarId("Dead");     // mark it
            justDied.emplace_back(mobName, mob);
            ++it;
            continue;
        }

        ++it;
    }

    // 2) reward only if something died this turn
    if (!justDied.empty())
    {
        const auto playersSnap = SnapshotPlayersLocked();

        for (auto const& [mobName, mob] : justDied)
        {
            if (!mob) continue;
            RewardPlayersForMobDeath(playersSnap, *mob, mobName);
        }
    }
 
    // 3) IMPORTANT: wave completion check must run EVERY turn
    bool anyAlive = false;
    for (auto const& [id, mob] : Mobs)
    {
        if (mob && mob->IsAlive()) { anyAlive = true; break; }
    }
    if(Players.empty()){
        return;
    }

    if (!anyAlive && MobToSpawnInWave <= 0)
    {
        NewWave(); // now it will be called each turn while waiting
    }
   
}


void CombatDirector::NewWave()
{
    DaraLog("TURN", "Wave over ... waiting "+std::to_string(WaveWaitTurns));
    if (WaveWaitTurns <= 0) WaveWaitTurns = DARA_WAVECOMPLETED_PAUSE;

    Phase = EGamePhase::WaveCompleted;
    WaveWaitTurns--;

    if (WaveWaitTurns <= 0) {
        Wave++;
        Phase = EGamePhase::Running;
        MobToSpawnInWave = Wave + static_cast<int>(GetRandomFloat(5.f,10.f));
        WaveWaitTurns = DARA_WAVECOMPLETED_PAUSE;
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
    //std::cout << "OpenSlots: "<< OpenSlotAmount << " FilledSlotAmount: "<< SpawnedMobsAmount<< std::endl;

}
void CombatDirector::BuildSpawnInfoMsg(std::string mobName, std::string  difficulty, std::string attackType)
{
    InfoMsg= mobName+ " spawned. Danger Level: "+difficulty+" Attck Type:"+attackType;
}

static std::string MakeMobInstanceId(const std::string& templateId)
{
    std::string uuid = GenerateUUID();           // you already have this
    return templateId + "-" + uuid.substr(0, 4); // short but unique enough for UI
}

void CombatDirector::SpawnMob(const std::string& templateId, int lane, int slot)
{
    if (templateId.empty()) return;

    // IMPORTANT: assume CacheMutex already held by caller (ResolveMobs)

    // Create instance from template (still using templateId to look up stats)
    auto mob = g_mobTemplates.CreateMobInstancePtr(templateId, lane, slot);
    if (!mob) return;

    // Assign instance id to the mob itself (add field + setter if missing)
    const std::string instanceId = MakeMobInstanceId(templateId);
    mob->SetInstanceId(instanceId);        // you add this
    mob->SetTemplateId(templateId);        // optional but very useful

    BuildSpawnInfoMsg(mob->GetName(), mob->GetDifficulty(), mob->GetAttackType());

    // Use instanceId as map key (no collision)
    Mobs.emplace(instanceId, std::move(mob));
}


void CombatDirector::ResolveSpawnMobs()
{
    if (Phase == EGamePhase::WaveCompleted) return;
    if (Phase != EGamePhase::Running) return; // optional guard
    
    int slot= RandSlot();
    
    GetFilledSlotArray();
    DaraLog("TURN", "Wave: " + std::to_string(Wave)+ " Turn: "+std::to_string(CurrentTurnId));

    if(!FilledSlotArray[0][slot] && ShallMobSpawn(CurrentTurnId) && !Players.empty() && MobToSpawnInWave>0){
        std::string mobId;
        if (MobToSpawnInWave <= 1) {
            mobId = g_mobTemplates.PickRandomBossForWave(Wave);
        }else{
            mobId = g_mobTemplates.PickRandomMobIdForWave(Wave);
        }
        MobToSpawnInWave--;
        if (mobId.empty()) {
            //throw std::runtime_error("No mob templates loaded");
            mobId = g_mobTemplates.PickRandomMobId();
            DaraLog("TURN", "End of possible mob waves... you should add more");
        }
        SpawnMob(mobId, 0, slot);

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
    // 1) Dead mobs entfernen oder alle entfernen wenn kein player mehr da (LOCK ist schon gehalten!)
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

    float kDamage = 5.0f;

    for (auto& [mobName, mob] : Mobs)
    {
        if (!mob) continue;
        // Optional: skip dead mobs
        // if (mob->GetHP() <= 0) continue;

        auto target = alivePlayers[pick(rng)];
        if (!target) continue;

        // Apply damage directly (NO second CacheMutex lock!)
        if(mob->ShouldAttack()){
            if(DARA_DEBUG_MOBCOMBAT)DaraLog("COMBAT", mob->GetName()+" should attack randomly " + target->GetName()+" Mob AttackType:"+mob->GetAttackType());
            mob->MobAttack(target);
            ApplyDamageToPlayerLocked(target->GetName(), kDamage);

            // Log globally (turn log)
            std::string logMsg= mobName + " attacks " + target->GetName() +
                " for " + std::to_string((int)kDamage) + " dmg.";
            outTurnLog.push_back(logMsg);

            if(DARA_DEBUG_COMBAT) DaraLog("MobAttack", logMsg);
        }
        // Optional per-entity log fields if you have them:
        // mob->SetLastAction("attack");
        // mob->SetCombatLog("Attacked " + target->GetName());
        // target->SetCombatLog("Hit by " + mobName);
    }
    
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


json CombatDirector::GetUIStateSnapshotJsonLocked(const std::string playerName) const
{
    std::lock_guard<std::mutex> lk(CacheMutex);

    UIState ui;

    ui.SetTurn(CurrentTurnId);
    ui.SetSlots(5); // your encounter grid width

    // Convert Players + Mobs → JSON
    // Wrong version? 
    nlohmann::json uiJson = ui.ToJson(Players, Mobs);
    
    /*
    nlohmann::json uiJson;
    uiJson["party"] = SerializePlayersLocked();
    uiJson["mobs"] = SerializeMobsLocked();
    */


     // ---- Add game-over attributes for the web UI ----
    // If you have: Running / GameOverPause / maybe other phases
    const bool isGameOver = (Phase == EGamePhase::GameOverPause);

    uiJson["turnId"] = CurrentTurnId;            // optional, but handy
    uiJson["wave"] = Wave;                       // shall be something for client
    uiJson["waveMobsLeft"] = MobToSpawnInWave;   // shall be something for client
    // Game Over handling
    if (Phase==EGamePhase::WaveCompleted){
        uiJson["phase"] = "wavecompleted";
    }else{
        uiJson["phase"] = (Phase == EGamePhase::Running) ? "running" : "gameover";
    }

    uiJson["gameOverReason"] = GameOverReason;
    uiJson["infoMsg"]= InfoMsg;
    uiJson["playerName"]= playerName;

    if (isGameOver)
    {
        auto now = std::chrono::steady_clock::now();
        auto msLeft = (GameOverUntil > now)
            ? std::chrono::duration_cast<std::chrono::milliseconds>(GameOverUntil - now).count()
            : 0;
        uiJson["restartInMs"] = msLeft;
    }else if(Phase==EGamePhase::WaveCompleted){
        uiJson["restartInMs"] = WaveWaitTurns;
    }else{
        uiJson["restartInMs"] = 0;
    }

    return uiJson;
}

bool CombatDirector::CheckGameOverLocked(std::string& outReason)
{
    if (Players.empty())
        return false;

    bool allDead = true;
    for (const auto& kv : Players)
    {
        if (kv.second && kv.second->IsAlive())
        {
            allDead = false;
            break;
        }
    }

    if (!allDead)
        return false;

    outReason = "All players are dead";
    ResetWave();

    // If we just entered game over, start the pause timer
    if (Phase != EGamePhase::GameOverPause)
    {
        Phase = EGamePhase::GameOverPause;
        GameOverReason = outReason;
        GameOverUntil = std::chrono::steady_clock::now() + GameOverPauseDuration;
        LastGameOverTurnId = CurrentTurnId;
    }

    return true;
}

void CombatDirector::ResetGameLocked()
{
    // Clear mobs and actions
    Mobs.clear();
    PendingActions.clear();
    BufferedActions.clear();

    // Reset players stats (keep them logged in)
    for (auto& [name, p] : Players)
    {
        if (!p) continue;

        // Use whatever your Combatant supports:
        // p->SetHP(MAXHP); p->SetEnergy(MAXENERGY); p->SetMana(MAXMANA);
        // If you don't have setters, add a method like p->ResetVitals(...)
        p->Revive(); // recommended helper
    }

    // Optional: clear log or keep it
    // Log.clear();

    // Restart turns
    CurrentTurnId = 0;
    Resolving = false;

    // Clear game over state
    Phase = EGamePhase::Running;
    GameOverReason.clear();
}