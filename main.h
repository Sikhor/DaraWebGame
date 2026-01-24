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


// Combatant Values
#define MAXMANA 100.f
#define MAXENERGY 100.f
#define MAXHP   1000.f
#define SPELLCOST 10.f
#define MELEECOST 10.f
#define DEVIATION 5.f
#define DAMAGEMIN 6.f
#define DAMAGEMAX 12.f


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

enum class ECombatantType
{
    Player,
    Mob,
    NPC
};
enum class ECondition
{
    None,
    Wounded,
    Burning,
    Poisoned,
    Mezzed,
    Defending,
    Fleeing,
    Incapacitated
};

float GetRandomFloat(float min, float max);
void ApplyRandomCost(float& current, float normal, float deviation);
std::string GenerateUUID();

class Combatant
{
protected:
    std::string Id;        // Unique id (PlayerName or EnemyId)
    std::string Name;      // Display name

    float HP = MAXHP;          // 0–100
    float Energy = MAXENERGY;      // 0–100
    float Mana = MAXMANA;        // 0–100
    float SpellManaMin=SPELLCOST;
    float MeleeManaMin=MELEECOST;
    ECombatantType Type= ECombatantType::Mob;
    std::vector<ECondition> Conditions;

public:
    Combatant(std::string name, ECombatantType type)
    {
        Id= GenerateUUID();
        Name= name;
        Type= type;

    }
    bool IsAlive() const
    {
        return HP > 0;
    }
    int GetHP() const
    {
        return static_cast<int>(HP);
    }
    int GetMana() const
    {
        return static_cast<int>(Mana);
    }
    int GetEnergy() const
    {
        return static_cast<int>(Energy);
    }
    float AttackMelee(std::string target)
    {
        float dmg= 0.f;
        if(Mana>MELEECOST){
            ApplyRandomCost(Energy, MELEECOST,DEVIATION);
            dmg= GetRandomFloat(DAMAGEMIN,DAMAGEMAX);
        }
        return dmg;
    }
    float AttackSpell(std::string target)
    {
        float dmg= 0.f;
        if(Mana>SPELLCOST){
            ApplyRandomCost(Mana, SPELLCOST,DEVIATION);
            dmg= GetRandomFloat(DAMAGEMIN,DAMAGEMAX);
        }
        return dmg;
    }
    void ApplyDamage(float dmg)
    {
        HP-=dmg;
        if(HP<0.f)HP=0.f;        
    }
};


using json = nlohmann::json;

static void TrimHistory(std::vector<json>& hist);
std::string readPromptFromFile(const std::string& filename);