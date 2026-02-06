#pragma once
#include <iostream>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <random>
#include "json.hpp"
#include "DaraConfig.h"

using json = nlohmann::json;

class Combatant;                         // forward declare
using CombatantPtr = std::shared_ptr<Combatant>;

// Combatant Values
inline constexpr float MAXMANA= 100.f;
inline constexpr float MAXENERGY= 100.f;
inline constexpr float MAXHP=   900.f;
inline constexpr float SPELLCOST= 10.f;
inline constexpr float MELEECOST= 10.f;
inline constexpr float DEVIATION= 5.f;
inline constexpr float STAT_BASEDAMAGE_PLAYER= 50.f;  
inline constexpr float STAT_BASEDEFENSE_PLAYER= 2.f;  

inline constexpr int INITIALPOTIONS= 10;  
inline constexpr int MEZZTURNS= 100;  

inline constexpr int MAXSLOTS=6;
inline constexpr int MAXLANES=10;

inline constexpr float MOBSPEED= 0.5f;

inline constexpr int XPPERLEVEL= 100;  

enum class ECombatantDifficulty
{
    Normal,
    Boss,
    GroupMob,
    GroupBoss,
    RaidMob,
    RaidBoss
};

enum class ECombatantAttackType
{
    Ranged,
    Melee,
    Combi,
    Spider,
    Insect,
    Healer
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

// --- enum to string helpers ---
static constexpr std::string_view ToString(ECombatantType t)
{
    switch (t) {
        case ECombatantType::Player: return "Player";
        case ECombatantType::Mob:    return "Mob";
        case ECombatantType::NPC:    return "NPC";
        default:                     return "Unknown ECombatantType";
    }
}

static constexpr std::string_view ToString(ECombatantDifficulty d)
{
    switch (d) {
        case ECombatantDifficulty::Normal:    return "Normal";
        case ECombatantDifficulty::Boss:      return "Boss";
        case ECombatantDifficulty::GroupMob:  return "GroupMob";
        case ECombatantDifficulty::GroupBoss: return "GroupBoss";
        case ECombatantDifficulty::RaidMob:   return "RaidMob";
        case ECombatantDifficulty::RaidBoss:  return "RaidBoss";
        default:                               return "Unknown ECombatantDifficulty";
    }
}

static constexpr std::string_view ToString(ECombatantAttackType a)
{
    switch (a) {
        case ECombatantAttackType::Ranged: return "Ranged";
        case ECombatantAttackType::Melee:  return "Melee";
        case ECombatantAttackType::Combi:  return "Combi";
        case ECombatantAttackType::Spider:  return "Spider";
        case ECombatantAttackType::Insect:  return "Insect";
        case ECombatantAttackType::Healer: return "Healer";
        default:                           return "Unknown ECombatantAttackType";
    }
}

static constexpr std::string_view ToString(ECondition c)
{
    switch (c) {
        case ECondition::None:          return "None";
        case ECondition::Wounded:       return "Wounded";
        case ECondition::Burning:       return "Burning";
        case ECondition::Poisoned:      return "Poisoned";
        case ECondition::Mezzed:        return "Mezzed";
        case ECondition::Defending:     return "Defending";
        case ECondition::Fleeing:       return "Fleeing";
        case ECondition::Incapacitated: return "Incapacitated";
        default:                        return "Unknown ECondition";
    }
}

static inline std::string ConditionsToString(const std::vector<ECondition>& conds)
{
    if (conds.empty()) return "[]";
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < conds.size(); ++i) {
        if (i) oss << ",";
        oss << ToString(conds[i]);
    }
    oss << "]";
    return oss.str();
}


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
    int Slot = 0; // 0-MAXSLOTS allowed
    bool Active=true;
    std::string AvatarId="MSAgent-Soldorn";
    std::string MobClass="MSAgent-Soldorn";
    float BaseDamage=STAT_BASEDAMAGE_PLAYER;
    float DamageModifier= 0.f;
    float BaseDefense=STAT_BASEDEFENSE_PLAYER;
    float DefenseModifier= 0.f;

    float Speed= MOBSPEED;
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
    int PotionAmount= INITIALPOTIONS;
    int MezzCounter= 0;

    int Level=0;
    int XP=0;
    int Credits= 0;

    std::string InstanceId="";
    std::string TemplateId="";


    ECombatantType Type = ECombatantType::Mob;
    ECombatantDifficulty Difficulty= ECombatantDifficulty::Normal;
    ECombatantAttackType AttackType= ECombatantAttackType::Melee;
    std::vector<ECondition> Conditions;

    void CheckStats();
    float GetRandomDamage();
    void LevelUp();

public:
    Combatant(const std::string& name, ECombatantType type);
    Combatant(const std::string& name, ECombatantType type, float hp, float energy, float mana, std::string mobClass="MSAgent-Soldorn", int lane=0, int slot=0);
    Combatant(){Combatant("unknown",ECombatantType::Player);}
    void InitFromMobTemplate(
        const std::string& mobClass,
        ECombatantAttackType attackType,
        ECombatantDifficulty difficulty,
        float speed,
        int maxHP,
        int maxEnergy,
        int maxMana,
        int baseDamage,
        int baseDefense
    );
    void Revive();

    bool IsAlive() const;
    std::string GetId(){return Id;}
    void InitId(std::string id){Id=id;}
    void InitName(std::string name){Name=name;}
    void InitPlayerType(){AttackType= ECombatantAttackType::Combi;}
    void InitXP(int xp){XP=xp;}
    void InitLevel(int level);
    void InitCredits(int credits){Credits= credits;}
    void InitPotions(int potions){PotionAmount= potions;}


    int GetHP() const;
    int GetMana() const;
    int GetEnergy() const;
    int GetActive() const { return Active ? 1 : 0; }   
    int GetMaxHP() const { return static_cast<int>(MaxHP); }
    int GetMaxEnergy() const { return static_cast<int>(MaxEnergy); }
    int GetMaxMana() const { return static_cast<int>(MaxMana); }
    int GetPotionAmount() const {return PotionAmount;}
    float GetBaseDefense() const{return BaseDefense;}
    float GetBaseDamage() const{return BaseDamage;}
    float GetCurrentDefense() const{return BaseDefense+DefenseModifier;}
    float GetCurrentDamage() const{return BaseDamage+DamageModifier;}

    // level, xp and credits
    int GetLevel() const { return Level; } // Placeholder
    int GetCredits() const { return Credits; } // Placeholder
    int GetXP() const { return XP; } // Placeholder

    void AddXP(int amount);
    void AddCredits(int amount){Credits+=amount;}
    void AddPotion(int amount){PotionAmount+=amount;};

    std::string GetAttackType()const;
    std::string GetDifficulty()const{return std::string(ToString(Difficulty));}

    void SetLane(int lane, int slot);
    int GetLane() const { return Lane; }
    int GetSlot() const { return Slot; }

    std::string GetAvatarId() const { return AvatarId; }    
    void SetAvatarId(std::string avatarId) { AvatarId=avatarId; }   

    void RegenTurn();
    void RegenTurnMob();

    std::string GetName() const;

    bool ShouldMove();
    bool ShouldAttack();
    bool IsMezzed() const {return MezzCounter>0;}
    std::string GetCondition() const {return IsMezzed()? "Mezzed" : "None" ;};
    void Debug();
    void DebugShort();

    void MobAttack(CombatantPtr target);
    float AttackMelee(CombatantPtr target);
    float AttackFireball(CombatantPtr target);
    float AttackShoot(CombatantPtr target);
    float AttackMezz(CombatantPtr target);
    void Heal(CombatantPtr target);
    void ReviveTarget(CombatantPtr target);
    void BuffDefense();
    void UsePotion();

    void ApplyDamage(float dmg);
    void ApplyHeal(float amount);
    void ReceiveMezz();

    float GetHPPct() const;
    float GetManaPct() const;
    float GetEnergyPct() const;
    json ToJson() const;

    void SetInstanceId(std::string instId){InstanceId= instId;}
    std::string GetInstanceId()const {return InstanceId;}
    void SetTemplateId(std::string templateId){TemplateId= templateId;}
    std::string GetTemplateId()const {return TemplateId;}

};





