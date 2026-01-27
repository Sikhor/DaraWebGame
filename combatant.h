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
inline constexpr float MAXMANA= 30.f;
inline constexpr float MAXENERGY= 30.f;
inline constexpr float MAXHP=   30.f;
inline constexpr float SPELLCOST= 10.f;
inline constexpr float MELEECOST= 10.f;
inline constexpr float DEVIATION= 5.f;
inline constexpr float DAMAGEMIN= 6.f;
inline constexpr float DAMAGEMAX= 12.f;  


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
    std::string Id;        // Unique id
    std::string Name;      // Display name

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
    Combatant(const std::string& name, ECombatantType type, float hp, float energy, float mana);

    bool IsAlive() const;
    int GetHP() const;
    int GetMana() const;
    int GetEnergy() const;

    int GetLevel() const { return 1; } // Placeholder
    int GetGold() const { return 100; } // Placeholder
    int GetExperience() const { return 0; } // Placeholder

    void RegenTurn();

    std::string GetName() const;

    float AttackMelee(const std::string& target);
    float AttackSpell(const std::string& target);

    void ApplyDamage(float dmg);

    float GetHPPercentage() const;
    float GetManaPercentage() const;
    float GetEnergyPercentage() const;
    json ToJson() const;
};


using CombatantPtr = std::shared_ptr<Combatant>;
