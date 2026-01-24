
#include "main.h"
#define DARA_DEBUG 1

auto AddCorsHeaders = [](httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
};

static std::unordered_map<std::string, std::shared_ptr<GameState>> g_stateByGameId;
static std::mutex g_stateMapMutex;


static std::unordered_map<std::string, std::vector<json>> g_historyByGameId;
static std::mutex g_historyMutex;

// Wie viel Kontext behalten? (z.B. 12 Messages = 6 Turns)
static constexpr size_t kMaxHistoryMessages = 12;

using CombatantPtr = std::unique_ptr<Combatant>;

std::unordered_map<std::string, CombatantPtr> Players;
std::unordered_map<std::string, CombatantPtr> Mobs;

std::string GetOpenAIKey()
{
    const char* v = std::getenv("OPENAI_API_KEY");
    if (!v || !*v) throw std::runtime_error("OPENAI_API_KEY not set");
    return std::string(v);
}

Combatant& GetOrCreatePlayer(const std::string& playerName)
{
    auto [it, inserted] = Players.try_emplace(
        playerName,
        std::make_unique<Combatant>(playerName, ECombatantType::Player)
    );

    return *it->second;
}
Combatant& GetOrCreateMob(const std::string& mobName)
{
    auto [it, inserted] = Players.try_emplace(
        mobName,
        std::make_unique<Combatant>(mobName, ECombatantType::Mob)
    );

    return *it->second;
}

std::string GenerateUUID()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t data[4] = { dist(gen), dist(gen), dist(gen), dist(gen) };

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << data[0] << "-"
        << std::setw(4) << ((data[1] >> 16) & 0xFFFF) << "-"
        << std::setw(4) << ((data[1] & 0xFFFF) | 0x4000) << "-" // version 4
        << std::setw(4) << ((data[2] & 0x3FFF) | 0x8000) << "-" // variant
        << std::setw(12) << data[3];

    return oss.str();
}


float GetRandomFloat(float min, float max)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}
void ApplyRandomCost(float& current, float normal, float deviation)
{
    float value= current;
    current-= GetRandomFloat(normal-deviation, normal+deviation);
    if(current <0.f) current=0.f;

}

static std::shared_ptr<GameState> GetGameState(const std::string& gameId)
{
    std::lock_guard<std::mutex> lock(g_stateMapMutex);
    auto& ptr = g_stateByGameId[gameId];
    if (!ptr) ptr = std::make_shared<GameState>();
    return ptr;
}

static std::string BuildCombinedTurnText(const std::vector<PendingAction>& actions)
{
    std::string combined = "TURN ACTIONS:\n";
    for (const auto& a : actions)
    {
        combined += "- Player: " + a.userName + "\n";
        combined += "  ActionId: " + a.actionId + "\n";
        combined += "  ActionMsg: " + a.actionMsg + "\n";
    }
    return combined;
}

void DebugMsg(const json& messages)
{
    if(DARA_DEBUG){
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

std::string ParseAIReply(const std::string aiResponse)
{
    std::string retString;
    std::ostringstream oss;
    json aiJson = json::parse(aiResponse);
    std::string narrative;

    try
    {
        narrative = aiJson.at("narrative").get<std::string>();
    }
    catch (const nlohmann::json::exception& e)
    {
        narrative = "[Narrative missing or invalid]";
        std::cerr << "JSON error (narrative): " << e.what() << "\n";
    }

    std::cout << "Narrative: " << narrative << "\n";

    for (const auto& intent : aiJson.at("enemy_intents"))
    {
        std::string enemyId = intent.at("enemyId").get<std::string>();
        std::string action  = intent.at("action").get<std::string>();
        std::string target  = intent.at("targetPlayer").get<std::string>();
        std::string reason  = intent.at("reason").get<std::string>();

        
        oss << "Enemy " << enemyId
                << " does " << action
                << " targeting " << target
                << " (" << reason << ")\n";
        

        // Here is where your combat logic kicks in
        if (action == "ATTACK")
        {
            //std::cout << "Attacking " <<std::endl;
            // ResolveAttack(enemyId, target);
        }
        else if (action == "DEFEND")
        {
            //std::cout << "Defend " <<std::endl;
            // ResolveDefend(enemyId);
        }
    }
    retString= oss.str();

    return aiJson;
}

std::string ContactAI(
    std::string gameId   = "0",
    int turnId = 0,
    std::string turnMsg= "All Actions of Players in this turn")
{
    //gameId, "TURN", "turn_commit", combined);

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

    // 2) Build user turn
    const std::string userTurn =
        "TurnId: " + std::to_string(turnId) +"\n"
        "PlayerActions: " + turnMsg;

    // 3) Build messages
    json messages = json::array();
    messages.push_back({{"role","system"}, {"content","You are a game master. React to player actions and describe what changed."}});
    messages.push_back({{"role","system"}, {"content", longPrompt}});

    for (const auto& m : historyCopy) {
        messages.push_back(m);
    }

    messages.push_back({{"role","user"}, {"content", userTurn}});
    DebugMsg(messages);


    // 4) Request payload
    json requestPayload = {
        {"model", "gpt-3.5-turbo"},
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
    
    std::cout << reply << std::endl;
    std::string formattedReply= reply; //ParseAIReply(reply);
    // 8) Update history only if we got a valid reply
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        auto& hist = g_historyByGameId[gameId];

        hist.push_back({{"role","user"}, {"content", userTurn}});
        hist.push_back({{"role","user"}, {"content", formattedReply}});

        TrimHistory(hist);
    }

    return reply;
}


static void ResolveTurnAsync(const std::string gameId, int turnNumber, std::vector<PendingAction> actionsCopy)
{
    static int turnId=0;
    // Call AI outside of locks
    const std::string combined = BuildCombinedTurnText(actionsCopy);

    // Reuse your ContactAI() by sending one combined "turn_commit" action
    std::string gmMsg = ContactAI(gameId, turnId, combined);
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

static void TrimHistory(std::vector<json>& hist)
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


int ContactDara() {
    // Set the URL and the JSON payload
    std::string url = "https://httpbin.org/post"; // test endpoint
    url = "http://game.daraempire.com/DaraPublicApi/DaraPublicApi"; // test endpoint
    
    nlohmann::json j;
    j["user"] = "Bernie";
    j["name"] = "Dara";
    j["role"] = "agent";
    j["stats"] = {{"strength", 10}, {"intelligence", 15}};

    std::cout << "JSON string:\n" << j.dump(4) << std::endl;
								    
    cpr::Response response = cpr::Post(
        cpr::Url{url},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{j.dump(4)}
    );

    // Output the response status and text
    std::cout << "Status code: " << response.status_code << std::endl;
    std::cout << "Response body:\n" << response.text << std::endl;

    return 0;
}

int main()
{
    httplib::Server server;
    
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

    try
    {
        auto body = json::parse(req.body);

        std::string gameId    = body.at("gameId").get<std::string>();
        std::string userName  = body.at("userName").get<std::string>();
        std::string actionId  = body.at("actionId").get<std::string>();
        std::string actionMsg = body.at("actionMsg").get<std::string>();

        auto state = GetGameState(gameId);

        int myTurn = 0;
        bool shouldStartResolver = false;
        std::vector<PendingAction> actionsToResolve;

        {
            std::lock_guard<std::mutex> lock(state->mtx);

            myTurn = state->currentTurn;

            // Prevent same player submitting twice in same turn (optional but recommended)
            for (const auto& a : state->pending)
            {
                if (a.userName == userName)
                {
                    json out = {
                        {"status", "error"},
                        {"message", "Player already submitted action for this turn"},
                        {"gameId", gameId},
                        {"turn", myTurn}
                    };
                    res.status = 409;
                    res.set_content(out.dump(), "application/json");
                    return;
                }
            }

            state->pending.push_back({userName, actionId, actionMsg});
            // Debug Message what has been received
            // std::cout << "Received Action:" << userName << " "<< actionId << " "<< actionMsg<< "\n";

            // If enough actions collected, launch resolver once
            if (!state->resolving && (int)state->pending.size() >= state->expectedPlayers)
            {
                state->resolving = true;
                actionsToResolve = state->pending; // copy
                state->pending.clear();
                state->currentTurn++; // immediately advance turn for next submissions
                shouldStartResolver = true;
            }
        }

        if (shouldStartResolver)
        {
            int resolvedTurn = myTurn;

            // Run AI call in the background so POST can return immediately
            std::thread([gameId, resolvedTurn, actions = std::move(actionsToResolve)]() mutable
            {
                ResolveTurnAsync(gameId, resolvedTurn, std::move(actions));

                // mark resolving=false afterwards
                auto st = GetGameState(gameId);
                std::lock_guard<std::mutex> lock(st->mtx);
                st->resolving = false;
            }).detach();
        }

        // Immediate ACK to client
        json out = {
            {"status", "submitted"},
            {"gameId", gameId},
            {"turn", myTurn},
            {"userName", userName},
            {"actionId", actionId}
        };

        res.set_content(out.dump(), "application/json");
    }
    catch (...)
    {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"Invalid JSON"})", "application/json");
    }
});

server.Get("/turnResult", [](const httplib::Request& req, httplib::Response& res)
{
    AddCorsHeaders(res);

    auto gameIdIt = req.get_param_value("gameId");
    auto turnIt   = req.get_param_value("turn");

    if (gameIdIt.empty() || turnIt.empty())
    {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"Missing gameId or turn"})", "application/json");
        return;
    }

    std::string gameId = gameIdIt;

    int turn = 0;
    try { turn = std::stoi(turnIt); }
    catch (...)
    {
        res.status = 400;
        res.set_content(R"({"status":"error","message":"Invalid turn"})", "application/json");
        return;
    }

    auto state = GetGameState(gameId);

    {
        std::lock_guard<std::mutex> lock(state->mtx);

        auto it = state->results.find(turn);
        if (it == state->results.end())
        {
            json out = {
                {"status", "pending"},
                {"gameId", gameId},
                {"turn", turn}
            };
            res.set_content(out.dump(), "application/json");
            return;
        }

        const auto& r = it->second;

        json actionsJson = json::array();
        for (const auto& a : r.actions)
        {
            actionsJson.push_back({
                {"userName", a.userName},
                {"actionId", a.actionId},
                {"actionMsg", a.actionMsg}
            });
        }

        json out = {
            {"status", "ok"},
            {"gameId", gameId},
            {"turn", r.turnNumber},
            {"actions", actionsJson},
            {"gameMasterMsg", r.gameMasterMsg}
        };

        res.set_content(out.dump(), "application/json");
    }
});




    std::cout << "REST API läuft auf http://0.0.0.0:"<<WEBSERVER_PORT<<"/action\n";
    //std::string apiKey = GetOpenAIKey();
    //std::cout << "API key length = " << apiKey.length() << "\n";

    server.listen("0.0.0.0", WEBSERVER_PORT);


}
