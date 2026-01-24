#include <iostream>
#include <cpr/cpr.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include "httplib.h"
#include "json.hpp"
#include "secret.h"

#define WEBSERVER_PORT 9050

using json = nlohmann::json;

auto AddCorsHeaders = [](httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
};

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
    std::string apiKey = CHATGPT_SECRET;
    std::string longPrompt;
    try {
        longPrompt = readPromptFromFile("prompt.txt");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return "cannot read prompt file";
    }



    // Build a large prompt (example)
    // std::string longPrompt = "Once upon a time in a galaxy far, far away, there was a Humrex Crafter that lost all his family to the evil Dara Empire and their warhords.";
    /* for (int i = 0; i < 1000; ++i) {
        longPrompt += "very ";
    }
    longPrompt += "strange planet.";
    */

    // Construct JSON request
   json requestPayload = {
        {"model", "gpt-3.5-turbo"},
        {"messages", {
            {{"role","system"}, {"content","You are a game master. React to player actions and describe what changed."}},
            {{"role","user"}, {"content", longPrompt}},
            {{"role","user"}, {"content",
                "Player: " + userName + "\nActionId: " + actionId + "\nActionMsg: " + actionMsg
            }}
        }},
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
    std::string apiKey = CHATGPT_SECRET;
    std::cout << "API key length = " << apiKey.length() << "\n";
    
    server.listen("0.0.0.0", WEBSERVER_PORT);


}
