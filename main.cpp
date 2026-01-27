#include <thread>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cpr/cpr.h>
#include "httplib.h"
#include "json.hpp"
#include "main.h"
#include "auth.h"
#include "parse.h"
#include "combatlog.h"
#include "CombatDirector.h"

CombatDirector* g_combatDirector = nullptr;


void TrimHistory(std::vector<json>& hist);


auto AddCorsHeaders = [](httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
};

static std::unordered_map<std::string, std::shared_ptr<GameState>> g_stateByGameId;
static std::mutex g_stateMapMutex;


static std::unordered_map<std::string, std::vector<json>> g_historyByGameId;
static std::mutex g_historyMutex;

// Wie viel Kontext behalten? (z.B. 12 Messages = 6 Turns)
static constexpr size_t kMaxHistoryMessages = 12;


std::unordered_map<std::string, CombatantPtr> Players;
std::mutex PlayersMutex;
std::unordered_map<std::string, CombatantPtr> Mobs;
std::mutex MobsMutex;

CombatLog CombatLogState(Players, PlayersMutex, Mobs, MobsMutex);
std::string Narrative= "Some cool story that I have here ..";

static std::string GeneratePlayerName(const std::string& base)
{
   static const std::vector<std::string> a = {
        "Zan","Ista","Vor","Kel","Ryn","Tar","Mal","Zen","Dro","Tun","Tro","Ari"
    };
    static const std::vector<std::string> b = {
        "drax","korr","naris","vane","thos","mir","dahl","rion","zeth","kara","lyx","qen"
    };

    // Zufallsgenerator (einmal pro Thread initialisiert)
    static thread_local std::mt19937 rng{ std::random_device{}() };

    std::uniform_int_distribution<size_t> distA(0, a.size() - 1);
    std::uniform_int_distribution<size_t> distB(0, b.size() - 1);
    std::uniform_int_distribution<int> distNum(100, 999);

    return a[distA(rng)] + b[distB(rng)] + "-" + std::to_string(distNum(rng));
}

std::string NewPlayer(const std::string& userName,
                      const std::string& displayName,
                      const std::string& pictureUrl)
{
    std::cout << "[LOGIN] Player: " << userName
              << " (" << displayName << ")\n";

    std::string playerName = GeneratePlayerName(displayName);
    // Your join logic:
    g_combatDirector->AddOrUpdatePlayer(playerName);
    //GetOrCreatePlayer(userName);

    // Optional: welcome narrative / combatlog
    CombatLogState.PushPlayerLog(playerName, "You arrive on Dara III.", "talk");
    return playerName;
}



std::string GetOpenAIKey()
{
    const char* v = std::getenv("OPENAI_API_KEY");
    if (!v || !*v) throw std::runtime_error("OPENAI_API_KEY not set");
    return std::string(v);
}
std::string GetOpenAIModel()
{
    const char* v = std::getenv("OPENAI_MODEL");
    if (!v || !*v) return "gpt-4o-mini"; // default
    return std::string(v);
}

static std::string TrimCopy(std::string s)
{
    auto notSpace = [](unsigned char c){ return !std::isspace(c); };

    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}
Combatant* GetRandomMob()
{
    std::lock_guard<std::mutex> lk(MobsMutex);

    if (Mobs.empty())
        return nullptr;

    static thread_local std::mt19937 rng{ std::random_device{}() };

    std::uniform_int_distribution<size_t> dist(0, Mobs.size() - 1);

    auto it = Mobs.begin();
    std::advance(it, dist(rng));

    return it->second.get(); // pointer is stable (shared_ptr owns it)
}

Combatant& GetOrCreatePlayer(const std::string& playerName)
{
    std::lock_guard<std::mutex> lk(PlayersMutex);
    if(playerName=="") throw std::runtime_error("Empty player name not allowed");

    auto [it, inserted] = Players.try_emplace(
        playerName,
        std::make_shared<Combatant>(playerName, ECombatantType::Player, MAXHP, MAXENERGY, MAXMANA)
    );

    return *it->second;
}
Combatant& GetOrCreateMob(const std::string& mobName)
{
    std::lock_guard<std::mutex> lk(MobsMutex);
    auto [it, inserted] = Mobs.try_emplace(
        mobName,
        std::make_shared<Combatant>(mobName, ECombatantType::Mob)
    );

    return *it->second;
}

std::string PlayerInfo(bool showAllInfo=false)
{
    std::lock_guard<std::mutex> lk(PlayersMutex);
    std::ostringstream oss;

    oss << "Players:\n";

    if (Players.empty())
    {
        oss << "- none\n";
        return oss.str();
    }

    for (const auto& [name, combatantPtr] : Players)
    {
        const Combatant& c = *combatantPtr;

        oss << "- " << name << " (" << (c.IsAlive() ? "Alive" : "Dead")<<")";
            if(showAllInfo){
            oss << " | HP:" << c.GetHP()
                << " | ENERGY:" << c.GetEnergy()
                << " | MANA:" << c.GetMana();
            }
            oss<< "\n";
    }

    return oss.str();
}

std::string MobInfo(bool showAllInfo=false)
{
    std::lock_guard<std::mutex> lk(MobsMutex);

    std::ostringstream oss;

    oss << "Enemies:\n";

    if (Mobs.empty())
    {
        oss << "- none\n";
        return oss.str();
    }

    for (const auto& [name, combatantPtr] : Mobs)
    {
        const Combatant& c = *combatantPtr;

        oss << "- " << name << " (" << (c.IsAlive() ? "Alive" : "Dead")<<")";
        if(showAllInfo){
        oss << " | HP:" << c.GetHP()
            << " | ENERGY:" << c.GetEnergy()
            << " | MANA:" << c.GetMana();
        }
        oss<< "\n";
    }

    return oss.str();
}


static std::shared_ptr<GameState> GetGameState(const std::string& gameId)
{
    std::lock_guard<std::mutex> lock(g_stateMapMutex);
    auto& ptr = g_stateByGameId[gameId];
    if (!ptr) ptr = std::make_shared<GameState>();
    return ptr;
}

static void StartPlayerCombatActions(std::string playerName, std::string action)
{
        if(action=="attack" && playerName!=""){

            Combatant *mob = GetRandomMob();
            if(mob==nullptr){
                if(DARA_DEBUG_ATTACKS) std::cout<<"No mobs available to attack"<<std::endl;
                return;
            }else{
                Combatant &player= GetOrCreatePlayer(playerName);
                if (mob->IsAlive() && player.IsAlive())
                {
                    if(DARA_DEBUG_ATTACKS) std::cout<<"ATTACK: "<<player.GetName() <<" attacks "<<mob->GetName()<<std::endl;

                    float dmg = player.AttackMelee("Chicka");
                    mob->ApplyDamage(dmg);
                    if(DARA_DEBUG_ATTACKS) std::cout<<"ATTACK RESULT: Damage "<<dmg <<" to the Mob "<<mob->GetName()<<std::endl;
                    CombatLogState.PushPlayerLog(
                        player.GetName(),
                        "You attack " + mob->GetName() + " for " + std::to_string(static_cast<int>(dmg)) + " damage.",
                        "attack"
                    );
                }
            }

        }else{
            if(DARA_DEBUG_ATTACKS) std::cout<<"NO ATTACK or empty Playername"<<std::endl;
        }

        // Regen HP, Mana, Energy per turn
        for(auto& [name, combatantPtr] : Players){
            Combatant &player= *combatantPtr;
            if(player.IsAlive()){
                player.RegenTurn();
            }
        }

}
static std::string BuildCombinedTurnText(const std::vector<PendingAction>& actions)
{
    std::string combined = "PlayerActions:\n";
    for (const auto& a : actions)
    {
        combined += "- " ;
        combined += a.userName +" ";
        combined += a.actionId;
        // combined += "  ActionMsg: " + a.actionMsg + "\n";
        combined += "\n";
        StartPlayerCombatActions(a.userName, a.actionId);
    }
    return combined;
}

void DebugMsgStats(std::string msg="")
{
    if(DARA_DEBUG_MSGSTATS){
        std::cout << "=== Current Game State ===\n";
        std::cout << PlayerInfo(true);
        std::cout << MobInfo(true);
        std::cout << "==========================\n";
    }
    if(DARA_DEBUG_AI_REPLIES){
        std::cout << "=======JSON===============\n";
        std::cout << msg << std::endl;
        std::cout << "=======END JSON===========\n";
    }

}
void DebugMsg(const json& messages)
{
    if(DARA_DEBUG_MESSAGES){
        int i=0;
        for (const auto& msg : messages)
        {
            if (i>1){
                std::cout << msg["content"].get<std::string>() << std::endl;
            }
            i++;
        }
    }
    std::cout <<"--------------------------------"<< std::endl;
}
void DebugNewMsg(const std::string& newMsg)
{
    if(DARA_DEBUG_NEWMESSAGES){
        std::cout << "NewMsg to AI:\n" << newMsg << std::endl;
    }
    std::cout <<"--------------------------------"<< std::endl;
}

static std::string ReadNextToken(const std::string& text, size_t pos)
{
    // skip spaces
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
        ++pos;

    size_t start = pos;

    // read until whitespace or punctuation that likely ends token
    while (pos < text.size())
    {
        char c = text[pos];
        if (std::isspace(static_cast<unsigned char>(c)))
            break;
        // stop on common punctuation that can appear after the target name
        if (c == ',' || c == ')' || c == '.' || c == ';' || c == ':')
            break;
        ++pos;
    }

    if (start >= text.size())
        return {};

    return text.substr(start, pos - start);
}

std::string ContactAI(
    std::string gameId   = "0",
    int turnId = 0,
    std::string turnMsg= "PlayerActions: NONE\n")
{
    // 0) Read API key + prompt
    std::string apiKey;
    try {
        apiKey = GetOpenAIKey();
    } catch (const std::exception& e) {
        std::cerr << "[ContactAI] Missing API key: " << e.what() << "\n";
        return "Error: OPENAI_API_KEY not set";
    }

    std::string longPrompt;
    try {
        longPrompt = readPromptFromFile("prompt.txt");
    } catch (const std::exception& e) {
        std::cerr << "[ContactAI] " << e.what() << "\n";
        return "Error: cannot read prompt file";
    }

    // 1) Copy history (thread-safe)
    std::vector<json> historyCopy;
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        historyCopy = g_historyByGameId[gameId];
        //DebugMsg(g_historyByGameId[gameId]);
    }

    // 2) Build user turn and new message to AI
    const std::string userTurn =
        //"TurnId: " + std::to_string(turnId) +"\n"
        "PlayerActions: " + turnMsg;
    std::string newMsgToAI= PlayerInfo() + MobInfo()+ userTurn + "\n";
    DebugNewMsg(newMsgToAI);

    // 3) Build messages
    json messages = json::array();
    // messages.push_back({{"role","system"}, {"content","You are a game master"}});
    messages.push_back({{"role","system"}, {"content", longPrompt}});

    for (const auto& m : historyCopy) {
        messages.push_back(m);
    }
    // now add new message to AI
    messages.push_back({{"role","user"}, {"content", newMsgToAI}});
    DebugMsg(messages);


    // 4) Request payload
    json requestPayload = {
        {"model", GetOpenAIModel()},
        {"messages", messages},
        {"temperature", 0.7}
    };

    // 5) Call API
    cpr::Response response;
    try {
        response = cpr::Post(
            cpr::Url{"https://api.openai.com/v1/chat/completions"},
            cpr::Header{
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + apiKey}
            },
            cpr::Body{requestPayload.dump()},
            cpr::Timeout{20000} // 20s timeout (adjust as you like)
        );
    } catch (const std::exception& e) {
        std::cerr << "[ContactAI] HTTP exception: " << e.what() << "\n";
        return "Error: OpenAI request failed (network exception)";
    }

    // 6) Handle non-200 without parsing as success JSON
    if (response.status_code != 200) {
        std::cerr << "[ContactAI] OpenAI error: HTTP " << response.status_code
                  << " Body: " << response.text << "\n";

        // Try to extract a useful error message if it's JSON, but don't crash if it isn't
        try {
            auto errJson = json::parse(response.text);
            if (errJson.contains("error") && errJson["error"].contains("message")) {
                return "Error: " + errJson["error"]["message"].get<std::string>();
            }
        } catch (...) {
            // ignore parse errors
        }

        return "Error: Calling OpenAI API failed (HTTP " + std::to_string(response.status_code) + ")";
    }

    // 7) Parse success response ONCE
    std::string reply;
    try {
        json jsonResponse = json::parse(response.text);

        // Defensive checks (avoid exceptions if schema changes)
        if (!jsonResponse.contains("choices") || jsonResponse["choices"].empty()) {
            std::cerr << "[ContactAI] Unexpected response (no choices): " << response.text << "\n";
            return "Error: OpenAI response had no choices";
        }

        auto& choice0 = jsonResponse["choices"][0];
        if (!choice0.contains("message") || !choice0["message"].contains("content")) {
            std::cerr << "[ContactAI] Unexpected response (no message/content): " << response.text << "\n";
            return "Error: OpenAI response missing content";
        }

        reply = choice0["message"]["content"].get<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "[ContactAI] JSON parse error: " << e.what()
                  << " Body: " << response.text << "\n";
        return "Error: Could not parse OpenAI response";
    }
    
    std::string formattedReply= "No reply set before parse\n";
    std::string error;
    json aiJson;
    if (!ParseAndValidateAIReply(reply, aiJson, error))
    {
        std::cerr << "AI reply invalid: " << error << "\n";
        // -> ignore AI turn or retry
        formattedReply= "Error in json reply from AI: " + error;
    }else{
        DebugMsgStats(reply);
        // set reply
        Narrative= aiJson["narrative"].get<std::string>();
        formattedReply = "Narrative:\n" + aiJson["narrative"].get<std::string>() + "\n";
        formattedReply+=PlayerInfo(true)+MobInfo(true);
        ParseActionsFromAI(aiJson, Players, Mobs);

        //if(DARA_DEBUG_AI_ACTIONS) std::cout<<"AI ACTION: "<<aiJson["enemy_intents"].get<std::string>()<<std::endl;

        // 8) Update history only if we got a valid reply
        // not sure if we need to pass back something the ai said
        {
            std::lock_guard<std::mutex> lock(g_historyMutex);
            auto& hist = g_historyByGameId[gameId];

            hist.push_back({{"role","user"}, {"content", newMsgToAI}});
            hist.push_back({{"role","assistant"}, {"content", reply}});
            
            TrimHistory(hist);
        }

    }

    return formattedReply+"[IMG:fireball] [SFX:explosion]"; // example for embedding images/sounds";

}

void InitialActions()
{
    std::srand((unsigned int)std::time(nullptr));
    // Example initial actions to setup the game state
    g_combatDirector->AddOrUpdateMob("Chicka");
    g_combatDirector->AddOrUpdateMob("Goblin");
    g_combatDirector->AddOrUpdateMob("Orc Warrior");
}

static void ResolveTurnAsync(const std::string gameId, int turnNumber, std::vector<PendingAction> actionsCopy)
{
    static int turnId=0;
    // Call AI outside of locks
    const std::string turnMsg = BuildCombinedTurnText(actionsCopy);


    // Reuse your ContactAI() by sending one combined "turn_commit" action
    std::string gmMsg = ContactAI(gameId, turnId, turnMsg);
    turnId++;

    auto state = GetGameState(gameId);
    {
        std::lock_guard<std::mutex> lock(state->mtx);

        TurnResult r;
        r.turnNumber = turnNumber;
        r.actions = std::move(actionsCopy);
        r.gameMasterMsg = std::move(gmMsg);
        r.createdAt = std::chrono::system_clock::now();

        state->results[turnNumber] = std::move(r);
    }
}

void TrimHistory(std::vector<json>& hist)
{
    if (hist.size() <= kMaxHistoryMessages) return;

    // entferne die ältesten
    size_t removeCount = hist.size() - kMaxHistoryMessages;
    hist.erase(hist.begin(), hist.begin() + static_cast<long>(removeCount));
}

std::string readPromptFromFile(const std::string& filename) {
    std::ifstream inFile(filename);
    if (!inFile) {
        throw std::runtime_error("Could not open prompt file: " + filename);
    }

    std::ostringstream ss;
    ss << inFile.rdbuf();
    return ss.str();
}



int main()
{
    g_combatDirector = new CombatDirector("DefaultGame");
    httplib::Server server;
    RegisterAuthRoutes(server);
    
    server.Options("/action",
        [](const httplib::Request&, httplib::Response& res)
        {
            AddCorsHeaders(res);
            res.status = 204; // No Content (Preflight OK)
        }
    );

server.Post("/action", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeaders(res);

    Session session;
    std::string serr;
    if (!GetSessionFromRequest(req, session, &serr)) {
        res.status = 401;
        res.set_content((json{{"status","error"},{"message",serr}}).dump(), "application/json");
        return;
    }

    const std::string& playerName = session.playerName;
    if (playerName.empty()) {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"Session has no playerName"})", "application/json");
        return;
    }

    auto body = json::parse(req.body);

    std::string gameId       = body.value("gameId", "0");
    std::string actionId     = body.value("actionId", "");
    std::string actionTarget = body.value("actionTarget", "");
    std::string actionMsg    = body.value("actionMsg", "");
    std::cout << "Received action from player " << playerName
              << ": " << actionId << " " << actionTarget << " Msg: " << actionMsg << "\n";

    std::string err;
    g_combatDirector->AddOrUpdatePlayer(playerName);
    bool ok = g_combatDirector->SubmitPlayerAction(playerName, actionId, actionTarget, actionMsg, &err);

    if (!ok || !err.empty()) {
        res.status = 400;
        res.set_content((json{{"status","error"},{"message", err}}).dump(), "application/json");
        return;
    }

    g_combatDirector->ApplyDamageToPlayer(playerName, 10.f, &err); // trigger condition updates

    res.status = 200;
    res.set_content((json{{"status","ok"},{"playerName",playerName}}).dump(), "application/json");
});


server.Get("/state", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeaders(res);
    res.status = 200;

    Session session;
    std::string err;
    if (!GetSessionFromRequest(req, session, &err)) {
        res.status = 401;
        res.set_content(
            (json{{"status","error"},{"message",err}}).dump(),
            "application/json"
        );
        return;
    }
    // DAS ist jetzt deine Player-Identität
    const std::string& playerName = session.playerName;

    json out = g_combatDirector->GetPlayerStateJson(playerName);

    res.set_content(out.dump(), "application/json");
    if(DARA_DEBUG_PLAYERSTATS)std::cout << "PlayerState for " << playerName << ": " << out.dump() <<std::endl;

    return;
 

});

server.Get("/story", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeaders(res);
    res.status = 200;
    
    nlohmann::json j;
    j["narrative"] = Narrative; 
    res.set_content(j.dump(), "application/json");
    return;

});

server.Get("/combatlog", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeaders(res);
    res.status = 204;

    const std::string body = CombatLogState.GetJsonCached();
    res.set_content(body, "application/json");

    if (true) 
    {
        res.status = 200;
        res.set_content(
            R"({
            "Players": [
                {"player":"Istako","hpPct":50,"combatLog":"Istako did something","action":"attack"},
                {"player":"Zendran","hpPct":20,"combatLog":"Zendran attacks Polta","action":"defend"},
                {"player":"Tunja","hpPct":56,"combatLog":"Tunja did something","action":"heal"},
                {"player":"Trollya","hpPct":80,"combatLog":"Trollya attacks Polta","action":"move"}
            ],
            "Mobs": [
                {"mob":"Polta","hpPct":10,"combatLog":"Polta was attacked by Istako","action":"attack"},
                {"mob":"Chicka","hpPct":50,"combatLog":"Chicka attacks Tunja","action":"defend"}
            ]
            })",
            "application/json"
            );
            return;
    }
    return;
 
});

server.Options("/register", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});
server.Options("/login", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});
server.Options("/logout", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});
server.Options("/me", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});

    g_combatDirector->Start();

    SetOnNewPlayerCallback(NewPlayer);
    InitialActions();
    std::cout << "REST API läuft auf http://0.0.0.0:"<<WEBSERVER_PORT<<"/action\n";
    server.listen("0.0.0.0", WEBSERVER_PORT);


}
