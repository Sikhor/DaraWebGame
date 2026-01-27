#pragma once
#include <iostream>
#include <cpr/cpr.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <string>
#include <chrono>
#include <unordered_set>
#include <random>
#include "httplib.h"
#include "json.hpp"
#include "combatant.h"

inline constexpr int WEBSERVER_PORT= 9050;
inline constexpr int PLAYERS_EXPECTED= 1;
inline constexpr int PLAYERS_MAX= 1;

// Debug Flags
inline constexpr int DARA_DEBUG_MESSAGES= 0;
inline constexpr int DARA_DEBUG_NEWMESSAGES= 0;
inline constexpr int DARA_DEBUG_ATTACKS= 0;
inline constexpr int DARA_DEBUG_AI_ACTIONS= 0;
inline constexpr int DARA_DEBUG_AI_REPLIES= 1;
inline constexpr int DARA_DEBUG_MSGSTATS= 0;
inline constexpr int DARA_DEBUG_PLAYERSTATS= 0;

struct UserAccount
{
    std::string userName;
    std::string passwordHash;
};


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


float GetRandomFloat(float min, float max);
void ApplyRandomCost(float& current, float normal, float deviation);
std::string GenerateUUID();


using json = nlohmann::json;

std::string readPromptFromFile(const std::string& filename);

Combatant& GetOrCreatePlayer(const std::string& playerName);
Combatant& GetOrCreateMob(const std::string& mobName);