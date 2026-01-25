#pragma once
#include <iostream>
#include <iomanip>
#include <cpr/cpr.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <string>
#include <random>


// Combatant Values
inline constexpr float MAXMANA= 100.f;
inline constexpr float MAXENERGY= 100.f;
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
    Combatant(std::string name, ECombatantType type, float hp, float energy, float mana)
    {
        Id= GenerateUUID();
        Name= name;
        Type= type;
        HP= hp;
        Energy= energy;
        Mana= mana; 
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
    void RegenTurn() 
    {
        HP+= GetRandomFloat(0.f, 3.f);
        Mana+= GetRandomFloat(0.f, 3.f);
        Energy+= GetRandomFloat(0.f, 3.f);
        if(HP>MAXHP) HP=MAXHP;
        if(Mana>MAXMANA) Mana=MAXMANA;
        if(Energy>MAXENERGY) Energy=MAXENERGY;
    }
    std::string GetName() const
    {
        return Name;
    }
    float AttackMelee(std::string target)
    {
        float dmg= 0.f;
        if(Energy>MELEECOST){
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


using CombatantPtr = std::shared_ptr<Combatant>;
