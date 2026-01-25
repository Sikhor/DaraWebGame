#include <iostream>
#include <thread>
#include <chrono>

#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;

void SendAction(
    httplib::Client& client,
    const std::string& gameId,
    const std::string& userName,
    const std::string& actionId,
    const std::string& actionMsg)
{
    json body = {
        {"gameId", gameId},
        {"userName", userName},
        {"actionId", actionId},
        {"actionMsg", actionMsg}
    };

    auto res = client.Post(
        "/action",
        body.dump(),
        "application/json"
    );

    if (res)
    {
        std::cout << "POST OK (" << res->status << ") for user "
                  << userName << std::endl;
                  try
                    {
                        json responseJson = json::parse(res->body);
                        std::cout << "Response JSON:\n"
                                << responseJson.dump(4) << "\n";
                    }
                    catch (const std::exception& e)
                    {
                        std::cout << "Response (non-JSON):\n"
                                << res->body << "\n";
                    }
    }
    else
    {
        std::cerr << "POST FAILED for user " << userName << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <delaySec>\n";
        return 1;
    }

    int delaySec = std::stoi(argv[1]);

    // Client (Host + Port anpassen!)
    httplib::Client client("http://127.0.0.1:9050");

    // Optional Timeouts
    client.set_connection_timeout(5);
    client.set_read_timeout(5);

    for(int i=0;i<5;i++){
        std::cout << "Sending action:"<<i<<"...\n";

        SendAction(client,
            "0",
            "Sikhor",
            "ATTACK",
            "testtext"
        );

        std::cout << "Sending second player action:"<<i<<"...\n";

        SendAction(client,
            "0",
            "Lanakhor",
            "ATTACK",
            "testtext"
        );
        std::cout << "Waiting " << delaySec << " seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(delaySec));
    }
    std::cout << "Done.\n";
    return 0;
}
