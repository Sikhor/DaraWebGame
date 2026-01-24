#pragma once
#include <iostream>
#include <cpr/cpr.h>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <condition_variable>
#include <optional>
#include <thread>
#include <chrono>
#include "httplib.h"
#include "json.hpp"

#define WEBSERVER_PORT 9050
#define PLAYERS_EXPECTED 2
#define PLAYERS_MAX 2

struct PendingAction
{
    std::string userName;
    std::string actionId;
    std::string actionMsg;
};

struct TurnResult
{
    int turnNumber = 0;
    std::string gameMasterMsg;
    std::vector<PendingAction> actions;
    std::chrono::system_clock::time_point createdAt;
};

struct GameState
{
    std::mutex mtx;

    int expectedPlayers = PLAYERS_EXPECTED;
    int currentTurn = 0;

    // Actions collected for the currentTurn
    std::vector<PendingAction> pending;

    // Store resolved results by turn number
    std::unordered_map<int, TurnResult> results;

    bool resolving = false; // prevents multiple resolver threads
};


using json = nlohmann::json;

static void TrimHistory(std::vector<json>& hist);
std::string readPromptFromFile(const std::string& filename);