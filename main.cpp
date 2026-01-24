#include <iostream>
#include "httplib.h"
#include "json.hpp"

#define WEBSERVER_PORT 9050

using json = nlohmann::json;

int main()
{
    httplib::Server server;

    server.Post("/action", [](const httplib::Request& req, httplib::Response& res)
    {
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

            res.set_content(
                R"({"status":"ok"})",
                "application/json"
            );
        }
        catch (const std::exception& e)
        {
            res.status = 400;
            res.set_content(
                R"({"status":"error","message":"Invalid JSON"})",
                "application/json"
            );
        }
    });

    std::cout << "REST API läuft auf http://0.0.0.0:8080/action\n";
    server.listen("0.0.0.0", WEBSERVER_PORT);
}