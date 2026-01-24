
#include "main.h"


using json = nlohmann::json;

auto AddCorsHeaders = [](httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
};



static std::unordered_map<std::string, std::vector<json>> g_historyByGameId;
static std::mutex g_historyMutex;
// Wie viel Kontext behalten? (z.B. 12 Messages = 6 Turns)
static constexpr size_t kMaxHistoryMessages = 12;

std::string GetOpenAIKey()
{
    const char* v = std::getenv("OPENAI_API_KEY");
    if (!v || !*v) throw std::runtime_error("OPENAI_API_KEY not set");
    return std::string(v);
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



std::string ContactAI(std::string gameId= "0", std::string userName= "Bernie", std::string actionId= "actionAttack", std::string actionMsg= "explore the strange planet") {
    std::string replyString= "";
    std::string apiKey = GetOpenAIKey();
    std::string longPrompt;
    try {
        longPrompt = readPromptFromFile("prompt.txt");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return "cannot read prompt file";
    }

    // 1) History für diese gameId holen (thread-safe)
    std::vector<json> historyCopy;
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        historyCopy = g_historyByGameId[gameId]; // copy
    }

    // 2) Aktuellen User-Input bauen (was passiert ist)
    const std::string userTurn =
        "Player: " + userName + "\n"
        "ActionId: " + actionId + "\n"
        "ActionMsg: " + actionMsg;

    // 3) messages: system + world prompt + history + current user turn
    json messages = json::array();
    messages.push_back({{"role","system"}, {"content","You are a game master. React to player actions and describe what changed."}});
    messages.push_back({{"role","system"}, {"content", longPrompt}});

    for (const auto& m : historyCopy) {
        messages.push_back(m);
    }

    messages.push_back({{"role","user"}, {"content", userTurn}});



    json requestPayload = {
        {"model", "gpt-3.5-turbo"},
        {"messages", messages},
        {"temperature", 0.7}
    };

    // Make POST request using cpr
    cpr::Response response = cpr::Post(
        cpr::Url{"https://api.openai.com/v1/chat/completions"},
        cpr::Header{
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + apiKey}
        },
        cpr::Body{requestPayload.dump()}
    );

    // Check response
    if (response.status_code == 200) {
        json jsonResponse = json::parse(response.text);
        std::string reply = jsonResponse["choices"][0]["message"]["content"];
        //std::cout << "ChatGPT says:\n" << reply << std::endl;
        replyString= reply;

    } else {
        std::cerr << "Error: " << response.status_code << "\n" << response.text << std::endl;
        replyString= "Error Calling chatGPT API";
    }

    // 5) Reply extrahieren
    json jsonResponse = json::parse(response.text);
    std::string reply = jsonResponse["choices"][0]["message"]["content"].get<std::string>();

    // 6) History updaten: user turn + assistant reply speichern (thread-safe)
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);

        auto& hist = g_historyByGameId[gameId];

        hist.push_back({{"role","user"}, {"content", userTurn}});
        hist.push_back({{"role","assistant"}, {"content", reply}});

        TrimHistory(hist);
    }

    return replyString;
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

            std::cout << "[ACTION RECEIVED]\n";
            std::cout << "GameId:    " << gameId << "\n";
            std::cout << "UserName:  " << userName << "\n";
            std::cout << "ActionId:  " << actionId << "\n";
            std::cout << "ActionMsg: " << actionMsg << "\n";
           // 1) AI call
            std::string gameMasterMsg = ContactAI(gameId, userName, actionId, actionMsg);

            // Optional: Server-log (nur Debug)
            std::cout << "GameMasterMsg: " << gameMasterMsg << "\n";

            // 2) JSON Response zurück an den Client
            json out = {
                {"status", "ok"},
                {"gameId", gameId},
                {"userName", userName},
                {"actionId", actionId},
                {"actionMsg", actionMsg},
                {"gameMasterMsg", gameMasterMsg}
            };

            res.set_content(out.dump(), "application/json");
        }
        catch (const std::exception& e)
        {
            AddCorsHeaders(res);
            res.status = 400;
            res.set_content(
                R"({"status":"error","message":"Invalid JSON"})",
                "application/json"
            );
        }
    });

    std::cout << "REST API läuft auf http://0.0.0.0:"<<WEBSERVER_PORT<<"/action\n";
    //std::string apiKey = GetOpenAIKey();
    //std::cout << "API key length = " << apiKey.length() << "\n";

    server.listen("0.0.0.0", WEBSERVER_PORT);


}
