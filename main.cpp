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
#include "sessions.h"
#include "MobTemplateStore.h"
#include "DaraConfig.h"
#include "character.h"
#include "CharacterRepository.h"
#include "CharacterDbWorker.h"

CombatDirector* g_combatDirector = nullptr;
MobTemplateStore g_mobTemplates;

CharacterDbWorker g_dbWorker;

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

std::string Narrative= "Some cool story that I have here ..";

extern CharacterDbWorker g_dbWorker;

static bool PostLoginInit(Session& s)
{
    // If you key character cache by email:
    if (s.eMail.empty()) return true;

    bool isDone = g_dbWorker.LoadUserBlocking(s.eMail, 800); // 800ms cap
    isDone= true; 
    return isDone; // never block login
}

void InitializeMobStore()
{
    std::string err;
    std::string filePath= (std::string)DARA_MOB_STORE;
    if(!g_mobTemplates.LoadFromFile(filePath, &err)) {
        DaraLog("FILE", "Loading mob templates from "+filePath);
        std::cerr << "Mob template load failed: " << err << "\n";
        DaraLog("ERROR", "Loading mob templates from "+filePath+ " failed");
        DaraLog("ERROR", "Error description: "+ err);
        return;
    }
    DaraLog("FILE", "Loaded mob templates");

}
/* NOT USED ATM but very handy
static std::string GeneratePlayerName()
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
} */

std::string NewPlayer(const std::string& userName,
                      const std::string& displayName,
                      Character character)
{
    //std::string playerName = GeneratePlayerName(displayName);
    // Your join logic:
    g_combatDirector->AddOrUpdatePlayer(displayName, character);

    DaraLog("LOGIN", "Created character :" + displayName + " for "+ userName);

    // Optional: welcome narrative / combatlog
    //CombatLogState.PushPlayerLog(playerName, "You arrive on Dara III.", "talk");
    return displayName;
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

/*
static std::shared_ptr<GameState> GetGameState(const std::string& gameId)
{
    std::lock_guard<std::mutex> lock(g_stateMapMutex);
    auto& ptr = g_stateByGameId[gameId];
    if (!ptr) ptr = std::make_shared<GameState>();
    return ptr;
}
    */

    /*
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
        //StartPlayerCombatActions(a.userName, a.actionId);
    }
    return combined;
}
    */

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

/*
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
*/

std::string ContactAI(
    std::string gameId   = "0",

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

    // 3) Build messages
    json messages = json::array();
    // messages.push_back({{"role","system"}, {"content","You are a game master"}});
    messages.push_back({{"role","system"}, {"content", longPrompt}});

    for (const auto& m : historyCopy) {
        messages.push_back(m);
    }
    // now add new message to AI
    std::string newMsgToAI= "Here is the next turn:\n"+ turnMsg +
                  "\nRespond with a JSON object containing at least the field 'narrative' describing the events of this turn. "
                  "Optionally include 'enemy_intents' describing what enemies plan to do next turn. "
                  "Ensure the JSON is properly formatted.";
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
        // set reply
        Narrative= aiJson["narrative"].get<std::string>();
        formattedReply = "Narrative:\n" + aiJson["narrative"].get<std::string>() + "\n";

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
}

/*
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
    */

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

void LogoutByToken(const std::string& token)
{
    Session s;
    if (!TryGetSession(token, s)) {
        return;
    }

    DaraLog("LOGOUT",s.playerName);
    // IMPORTANT: Remove player by playerName (because you added playerName to CombatDirector)
    if (!s.playerName.empty()) {
        g_combatDirector->RemovePlayer(s.playerName);
    }

    RemoveSession(token);
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
    DaraLog("Action", "Received action from player "+ playerName
              + ": " + actionId + " " + actionTarget + " ActionMsg: " + actionMsg );

    std::string err;


    if(g_combatDirector->GetPhase()==EGamePhase::GameOverPause){
        res.status = 200;
        res.set_content((json{{"status","ok"},{"playerName",playerName}}).dump(), "application/json");
        return;
    }
    bool ok = g_combatDirector->SubmitPlayerAction(playerName, actionId, actionTarget, actionMsg, &err);

    if (!ok || !err.empty()) {
        res.status = 400;
        res.set_content((json{{"status","error"},{"message", err}}).dump(), "application/json");
        return;
    }
  
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
    //const std::string& playerName = session.playerName;

    json out = g_combatDirector->GetUIStateSnapshotJsonLocked();
    if(DARA_DEBUG_MOBSTATS) std::cout << "GetUIStateSnapshotJsonLocked: " << out["mobs"].dump(2) <<std::endl;
    if(DARA_DEBUG_PLAYERSTATS) std::cout << "GetUIStateSnapshotJsonLocked: " << out["party"].dump(2) <<std::endl;
    if(DARA_DEBUG_FULLSTATE) std::cout << "/state reply: " << out.dump(2) <<std::endl;

    res.set_content(out.dump(), "application/json");

    return;
 

});

server.Options("/auth/logout", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});

server.Post("/auth/logout", [](const httplib::Request& req, httplib::Response& res){
    AddCorsHeadersAuth(res);

    const std::string token = GetBearerToken(req); // use the helper from auth.h/auth.cpp
    if (token.empty()) {
        res.status = 401;
        res.set_content(R"({"status":"error","message":"Missing Authorization Bearer token"})", "application/json");
        return;
    }

    Session s;
    if (!TryGetSession(token, s)) {
        res.status = 401;
        res.set_content(R"({"status":"error","message":"Invalid or expired session"})", "application/json");
        return;
    }
    DaraLog("LOGOUT", s.userName+" "+s.playerName);
    if (!s.playerName.empty()) {
        g_combatDirector->RemovePlayer(s.playerName);
    }

    RemoveSession(token);

    res.status = 200;
    res.set_content(R"({"status":"ok"})", "application/json");
});

server.Get("/story", [](const httplib::Request& req, httplib::Response& res)
{
    //ATTENTION REMOVE NEXT LINE WHEN IMPLEMENTING THIS!
    (void)req;
    AddCorsHeaders(res);
    res.status = 200;
    
    /*
    json logs = g_combatDirector->GetLogTailJson(30);
    for(auto& entry : logs){
        if(DARA_DEBUG_STORYLOG)std::cout << "StoryLog:" << entry["turnId"] << " " << entry["text"] << std::endl;
    }
    */

    nlohmann::json n;
    n["narrative"] = "what a wonderful world"; 
    res.set_content(n.dump(), "application/json");
    return;

});

server.Get("/combatlog", [](const httplib::Request& req, httplib::Response& res)
{
    //ATTENTION REMOVE NEXT LINE WHEN IMPLEMENTING THIS!
    (void)req;
    AddCorsHeaders(res);
    res.status = 204;

    json out= g_combatDirector->GetCombatState();
    res.status = 200;
    res.set_content(
        out.dump(),
        "application/json"
    );
    if(DARA_DEBUG_COMBAT) std::cout << "CombatLog: " << out.dump() <<std::endl;
    return;

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
server.Options("/characters", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});

server.Options("/characters/create", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});
server.Options("/characters/select", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});
server.Options("/characters/me", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});
server.Options("/characters/delete", [](const httplib::Request&, httplib::Response& res){
    AddCorsHeadersAuth(res);
    res.status = 204;
});

server.Get("/characters", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeadersAuth(res);

    Session session;
    std::string err;
    if (!GetSessionFromRequest(req, session, &err)) {
        res.status = 401;
        res.set_content((json{{"status","error"},{"message",err}}).dump(), "application/json");
        return;
    }

    // session.eMail = userKey (email or google sub)
    const std::string userKey = session.eMail;

    json out;
    out["status"] = "ok";
    if (!HasCharactersCachedForUser(userKey)) {
        g_dbWorker.RequestLoadUser(userKey);
        out["charactersLoading"] = true;
        out["characters"] = json::array();
    } else {
        out["charactersLoading"] = false;
        out["characters"] = SerializeCharactersForUser(userKey);
    }

    res.status = 200;
    res.set_content(out.dump(), "application/json");
});

server.Post("/characters/select", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeadersAuth(res);

    const std::string token = GetBearerToken(req);
    if (token.empty()) {
        res.status = 401;
        res.set_content(R"({"status":"error","message":"Missing Authorization Bearer token"})", "application/json");
        return;
    }

    Session session;
    if (!TryGetSession(token, session)) {
        res.status = 401;
        res.set_content(R"({"status":"error","message":"Invalid or expired session"})", "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"Invalid JSON body"})", "application/json");
        return;
    }

    //cached access of usermails characters
    // Ensure characters are cached for this user (key = email)
    if (!HasCharactersCachedForUser(session.eMail))
    {
        // async request to load from DB; client can retry shortly
        g_dbWorker.RequestLoadUser(session.eMail);

        json out;
        out["status"] = "loading";
        out["charactersLoading"] = true;
        out["characters"] = json::array();

        res.status = 200;
        res.set_content(out.dump(), "application/json");
        return;
    }
    // EXPECTED REQUEST BODY:
    // { "characterId":"..." }
    //
    // RETURNS:
    // { "status":"ok", "selectedCharacterId":"...", "character":{...} }

    std::string characterId = TrimCopy(body.value("characterId", ""));
    if (characterId.empty()) {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"characterId is required"})", "application/json");
        return;
    }else{
        DaraLog("LOGIN", session.userName+" has chosen characterId:"+characterId);
        DaraLog("LOGIN", "Session.sub:"+session.sub);
        DaraLog("LOGIN", "Session.userName:"+session.userName);
        DaraLog("LOGIN", "Session.eMail:"+session.eMail);
        DaraLog("LOGIN", "Session.name:"+session.name);
    }

    Character chosen;
    bool found = false;
    
    {
        std::lock_guard<std::mutex> lock(g_charsMutex);
        auto it = g_charsByUser.find(session.eMail);
        if (it != g_charsByUser.end()) {
            auto& vec = it->second;
            Character* c = FindCharacterLocked(vec, characterId);
            if (c) { chosen = *c; found = true; }
        }
    }

    if (!found) {
        res.status = 404;
        res.set_content(R"({"status":"error","message":"Character not found"})", "application/json");
        return;
    }

    // Persist selection in session
    session.characterId = chosen.characterId;
    session.characterName = chosen.characterName;

    // IMPORTANT: you must update your session store for this token.
    UpdateSessionByToken(token, session);
    SetSessionCharacter(token, chosen.characterId, chosen.characterName);

    // Now that a character is selected, you can join combat as that character:
    NewPlayer(session.userName, chosen.characterName, chosen);

    json out;
    out["status"] = "ok";
    out["selectedCharacterId"] = chosen.characterId;
    out["character"] = SerializeSelectedCharacterLockedForUser(session.eMail, chosen.characterId);


    res.status = 200;
    res.set_content(out.dump(), "application/json");
});

server.Post("/characters/create", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeadersAuth(res);

    const std::string token = GetBearerToken(req);
    if (token.empty()) {
        res.status = 401;
        res.set_content(R"({"status":"error","message":"Missing Authorization Bearer token"})", "application/json");
        return;
    }

    Session session;
    if (!TryGetSession(token, session)) {
        res.status = 401;
        res.set_content(R"({"status":"error","message":"Invalid or expired session"})", "application/json");
        return;
    }

    json body;
    try { body = json::parse(req.body); }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"Invalid JSON body"})", "application/json");
        return;
    }

    DaraLog("CREATECHAR", "eMail "+ session.eMail+ " Response.body="+body.dump(2));

    // Validate request fields
    const std::string charactername  = body.value("characterName", "unknown");
    const std::string characterclass = body.value("characterClass", "Agent");
    const std::string characteravatar= body.value("avatar", "MSAgent-Soldorn");

    if (charactername.empty() || characterclass.empty())
        throw std::runtime_error("Missing characterName or characterClass");

    // DB write (safe here: character screen, not in match tick)
    int newId = CreateCharacter(session.userName, session.eMail, charactername, characterclass, characteravatar);

    DaraLog("CREATECHAR", "eMail"+ session.eMail+ "CharacterName:"+charactername+" New Id:"+std::to_string(newId));

    json out;
    out["ok"] = true;
    out["characterId"] = std::to_string(newId);
    out["characterName"]= charactername;
    out["avatar"]= characteravatar;


    res.status = 200;
    res.set_content(out.dump(), "application/json");
});

    SetPostLoginHook(PostLoginInit);
    InitializeMobStore();
    g_combatDirector->Start();
    g_dbWorker.Start();

    InitialActions();
    DaraLog("SERVER", "REST API on http://0.0.0.0:"+ std::to_string(WEBSERVER_PORT)+"  e.g. /action");
    server.listen("0.0.0.0", WEBSERVER_PORT);

    g_dbWorker.Stop();

}
