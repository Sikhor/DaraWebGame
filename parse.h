#pragma once
#include "json.hpp"
#include <string>   
#include "main.h"


bool ParseAndValidateAIReply(
    const std::string& aiReply,
    json& outJson,
    std::string& outError);


void ParseActionsFromAI(
    const json& aiJson,
    std::unordered_map<std::string, CombatantPtr>& Players,
    std::unordered_map<std::string, CombatantPtr>& Mobs);