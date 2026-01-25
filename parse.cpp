#include "globals.h"
#include "parse.h"

void ParseActionsFromAI(
    const json& aiJson,
    std::unordered_map<std::string, CombatantPtr>& Players,
    std::unordered_map<std::string, CombatantPtr>& Mobs)
{
    if (aiJson.contains("enemy_intents") && aiJson["enemy_intents"].is_array())
    {
        for (const auto& intent : aiJson["enemy_intents"])
        {
            std::string enemyId      = intent.value("enemyId", "");
            std::string action       = intent.value("action", "WAIT");
            std::string targetPlayer = intent.value("targetPlayer", "");
            std::string reason       = intent.value("reason", "");

            if (DARA_DEBUG_AI_ACTIONS)
            {
                std::cout << "Enemy " << enemyId
                          << " will " << action
                          << " on player " << targetPlayer
                          << " because: " << reason << "\n";
            }
            if(targetPlayer.empty()){
                // no action to take
                continue;
            }
            if(enemyId.empty()){
                // no action to take
                continue;
            }
            Combatant& mob = GetOrCreateMob(enemyId);
            Combatant& player = GetOrCreatePlayer(targetPlayer);
            if (action == "ATTACK")
            {
                float dmg = mob.AttackMelee(player.GetName());
                player.ApplyDamage(dmg);
            }

        }
    }
    if (aiJson.contains("spawns") && aiJson["spawns"].is_array())
    {
        for (const auto& intent : aiJson["spawns"])
        {
            std::string enemyId      = intent.value("enemyId", "");
            std::string name       = intent.value("name", "small mob");

            if (DARA_DEBUG_AI_ACTIONS)
            {
                std::cout << "New Enemy " << name<< "\n";
            }
            if(name.empty()){
                // no action to take
                continue;
            }
            GetOrCreateMob(name);
        }

    }
    if (aiJson.contains("despawns") && aiJson["despawns"].is_array())
    {
        for (const auto& intent : aiJson["despawns"])
        {
            std::string enemyId      = intent.value("enemyId", "");
            std::string reason       = intent.value("reason", "");

            if (DARA_DEBUG_AI_ACTIONS)
            {
                std::cout << "Despawning Enemy " << enemyId
                          << " because: " << reason << "\n";
            }
            if(enemyId.empty()){
                // no action to take
                continue;
            }
            std::lock_guard<std::mutex> lk(MobsMutex);
            auto it = Mobs.find(enemyId);
            if (it != Mobs.end())
            {
                Mobs.erase(it);
            }
        }

    }


    //std::cout << "END OF ParseActionsFromAI\n";
}

bool ParseAndValidateAIReply(
    const std::string& aiReply,
    json& outJson,
    std::string& outError)
{
    // 1) Parse JSON
    try
    {
        outJson = json::parse(aiReply);
    }
    catch (const json::parse_error& e)
    {
        outError = std::string("JSON parse error: ") + e.what();
        return false;
    }

    // 2) Root must be object
    if (!outJson.is_object())
    {
        outError = "Root JSON is not an object";
        return false;
    }

    // 3) Required top-level keys
    const char* requiredKeys[] =
    {
        "narrative",
        "enemy_intents",
        "spawns",
        "despawns"
    };

    for (const char* key : requiredKeys)
    {
        if (!outJson.contains(key))
        {
            outError = std::string("Missing key: ") + key;
            return false;
        }
    }

    // 4) narrative
    if (!outJson["narrative"].is_string())
    {
        outError = "narrative must be a string";
        return false;
    }

    // 5) enemy_intents
    if (!outJson["enemy_intents"].is_array())
    {
        outError = "enemy_intents must be an array";
        return false;
    }

    for (const auto& intent : outJson["enemy_intents"])
    {
        if (!intent.is_object())
        {
            outError = "enemy_intents entry is not an object";
            return false;
        }

        // Required fields
        if (!intent.contains("enemyId") ||
            !intent.contains("action") ||
            !intent.contains("targetPlayer") ||
            !intent.contains("reason"))
        {
            outError = "enemy_intents entry missing required fields";
            return false;
        }

        if (!intent["enemyId"].is_string())
        {
            outError = "enemyId must be string";
            return false;
        }

        if (!intent["action"].is_string())
        {
            outError = "action must be string";
            return false;
        }

        if (!(intent["targetPlayer"].is_string() || intent["targetPlayer"].is_null()))
        {
            outError = "targetPlayer must be string or null";
            return false;
        }

        if (!intent["reason"].is_string())
        {
            outError = "reason must be string";
            return false;
        }
    }

    // 6) spawns
    if (!outJson["spawns"].is_array())
    {
        outError = "spawns must be an array";
        return false;
    }

    for (const auto& spawn : outJson["spawns"])
    {
        if (!spawn.is_object())
        {
            outError = "spawns entry is not an object";
            return false;
        }

        if (!spawn.contains("enemyId") ||
            !spawn.contains("name") ||
            !spawn.contains("type"))
        {
            outError = "spawns entry missing required fields";
            return false;
        }

        if (!spawn["enemyId"].is_string() ||
            !spawn["name"].is_string() ||
            !spawn["type"].is_string())
        {
            outError = "spawns fields must be strings";
            return false;
        }
    }

    // 7) despawns
    if (!outJson["despawns"].is_array())
    {
        outError = "despawns must be an array";
        return false;
    }

    for (const auto& despawn : outJson["despawns"])
    {
        if (!despawn.is_object())
        {
            outError = "despawns entry is not an object";
            return false;
        }

        if (!despawn.contains("enemyId") ||
            !despawn.contains("reason"))
        {
            outError = "despawns entry missing required fields";
            return false;
        }

        if (!despawn["enemyId"].is_string() ||
            !despawn["reason"].is_string())
        {
            outError = "despawns fields must be strings";
            return false;
        }
    }

    // ✅ Valid
    return true;
}