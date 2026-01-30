#pragma once
#include <iostream>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <random>
#include "json.hpp"

using json = nlohmann::json;
// Combatant Values
inline constexpr float MAXMANA= 100.f;
inline constexpr float MAXENERGY= 100.f;
inline constexpr float MAXHP=   100.f;
inline constexpr float SPELLCOST= 10.f;
inline constexpr float MELEECOST= 10.f;
inline constexpr float DEVIATION= 5.f;
inline constexpr float DAMAGEMIN= 6.f;
inline constexpr float DAMAGEMAX= 12.f;  

inline constexpr int MAXSLOTS=4;
inline constexpr int MAXLANES=3;

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

struct Lane
{
    std::string name;     // "LONG RANGE"
    std::string cssClass; // "long"
    std::string range;    // "8–10m"
};


float GetRandomFloat(float min, float max);
void ApplyRandomCost(float& current, float normal, float deviation);
std::string GenerateUUID();

class Combatant
{
protected:
    std::string Id;        // Unique id
    std::string Name;      // Display name

    int Lane = 0; // 0=Short,1=Medium,2=Long
    int Slot = 0; // 0-4 allowed
    bool Active=true;
    std::string AvatarId="MSAgent-Soldorn";
    std::string mobClass="MSAgent-Soldorn";

    float Speed= 0.3f;
    float CurrentField= 0.f;

    float HP = MAXHP;
    float Energy = MAXENERGY;
    float Mana = MAXMANA;

    float MaxHP = MAXHP;
    float MaxEnergy = MAXENERGY;
    float MaxMana = MAXMANA;

    float HPPercentage = 100.f;
    float EnergyPercentage = 100.f;
    float ManaPercentage = 100.f;

    float SpellManaMin = SPELLCOST;
    float MeleeManaMin = MELEECOST;

    ECombatantType Type = ECombatantType::Mob;
    std::vector<ECondition> Conditions;

public:
    Combatant(const std::string& name, ECombatantType type);
    Combatant(const std::string& name, ECombatantType type, float hp, float energy, float mana, std::string mobClass="MSAgent-Soldorn", int lane=0, int slot=0);

    bool IsAlive() const;
    int GetHP() const;
    int GetMana() const;
    int GetEnergy() const;
    int GetActive() const { return Active ? 1 : 0; }   

    int GetLevel() const { return 1; } // Placeholder
    int GetGold() const { return 100; } // Placeholder
    int GetExperience() const { return 0; } // Placeholder

    void SetLane(int lane, int slot);
    int GetLane() const { return Lane; }
    int GetSlot() const { return Slot; }

    std::string GetAvatarId() const { return AvatarId; }    
    void SetAvatarId(std::string avatarId) { AvatarId=avatarId; }   

    void RegenTurn();

    std::string GetName() const;

    bool ShouldMove();

    float AttackMelee(const std::string& target);
    float AttackSpell(const std::string& target);

    void ApplyDamage(float dmg);

    float GetHPPct() const;
    float GetManaPct() const;
    float GetEnergyPct() const;
    json ToJson() const;
};


using CombatantPtr = std::shared_ptr<Combatant>;
