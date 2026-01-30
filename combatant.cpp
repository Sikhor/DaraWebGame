#include "combatant.h"

float GetRandomFloat(float min, float max)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}
void ApplyRandomCost(float& current, float normal, float deviation)
{
    float value= current;
    current-= GetRandomFloat(normal-deviation, normal+deviation);
    if(current <0.f) current=0.f;

}

std::string GenerateUUID()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t data[4] = { dist(gen), dist(gen), dist(gen), dist(gen) };

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << data[0] << "-"
        << std::setw(4) << ((data[1] >> 16) & 0xFFFF) << "-"
        << std::setw(4) << ((data[1] & 0xFFFF) | 0x4000) << "-" // version 4
        << std::setw(4) << ((data[2] & 0x3FFF) | 0x8000) << "-" // variant
        << std::setw(12) << data[3];

    return oss.str();
}



Combatant::Combatant(const std::string& name, ECombatantType type)
{
    Id = GenerateUUID();
    Name = name;
    Type = type;
}

Combatant::Combatant(const std::string& name, ECombatantType type, float hp, float energy, float mana, std::string mobClass, int lane, int slot)
{
    Id = GenerateUUID();
    Name = name;
    Type = type;
    AttackType= ECombatantAttackType::Melee;
    Difficulty= ECombatantDifficulty::Normal;
    AvatarId= mobClass;
    MobClass= mobClass;

    HP = hp;
    MaxHP= hp;
    Energy = energy;
    MaxEnergy= energy;
    Mana = mana;
    MaxMana= mana;
    Lane= lane;
    Slot= slot;
}
void Combatant::Revive()
{
    HP=MaxHP;
    Energy= MaxEnergy;
    Mana= MaxMana;
    AvatarId=MobClass;
}

bool Combatant::IsAlive() const
{
    return HP > 0.f;
}

int Combatant::GetHP() const
{
    return static_cast<int>(HP);
}

int Combatant::GetMana() const
{
    return static_cast<int>(Mana);
}

int Combatant::GetEnergy() const
{
    return static_cast<int>(Energy);
}

void Combatant::RegenTurn()
{
    HP += GetRandomFloat(0.f, 3.f);
    Mana += GetRandomFloat(0.f, 3.f);
    Energy += GetRandomFloat(0.f, 3.f);

    if (HP > MaxHP) HP = MaxHP;
    if (Mana > MaxMana) Mana = MaxMana;
    if (Energy > MaxEnergy) Energy = MaxEnergy;
}

std::string Combatant::GetName() const
{
    return Name;
}

float Combatant::AttackMelee(const std::string& target)
{
    (void)target; // remove if you use it later

    float dmg = 0.f;
    if (Energy > MELEECOST) {
        ApplyRandomCost(Energy, MELEECOST, DEVIATION);
        dmg = GetRandomFloat(DAMAGEMIN, DAMAGEMAX);
    }
    return dmg;
}

float Combatant::AttackSpell(const std::string& target)
{
    (void)target;

    float dmg = 0.f;
    if (Mana > SPELLCOST) {
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        dmg = GetRandomFloat(DAMAGEMIN, DAMAGEMAX);
    }
    return dmg;
}

void Combatant::ApplyDamage(float dmg)
{
    HP -= dmg;
    if (HP <= 0.f){ 
        HP = 0.f;
        AvatarId= DARA_DEAD_AVATAR_PLAYER;
    }
}

float Combatant::GetHPPct() const
{
    return (MaxHP > 0.f) ? (HP / MaxHP) * 100.f : 0.f;
}

float Combatant::GetManaPct() const
{
    return (MaxMana > 0.f) ? (Mana / MaxMana) * 100.f : 0.f;
}

float Combatant::GetEnergyPct() const
{
    return (MaxEnergy > 0.f) ? (Energy / MaxEnergy) * 100.f : 0.f;
}

json Combatant::ToJson() const
{
    json j;
    j["Id"] = Id;
    j["Name"] = Name;
    j["Type"] = static_cast<int>(Type);
    j["HP"] = HP;
    j["MaxHP"] = MaxHP;
    j["Energy"] = Energy;
    j["MaxEnergy"] = MaxEnergy;
    j["Mana"] = Mana;
    j["MaxMana"] = MaxMana;
    j["HPPct"] = GetHPPct();
    j["EnergyPct"] = GetEnergyPct();
    j["ManaPct"] = GetManaPct();
    j["Conditions"] = json::array();
    for (const auto& condition : Conditions) {
        j["Conditions"].push_back(static_cast<int>(condition));
    }
    return j;
}

void Combatant::SetLane(int lane, int slot)
{
    Lane = lane;
    Slot = slot;
}

bool Combatant::ShouldMove()
{
    CurrentField+=Speed;
    return CurrentField>(Lane+1) && Lane<MAXLANES;
}

bool Combatant::ShouldAttack()
{
    switch (AttackType)
    {
        case ECombatantAttackType::Melee:
            // Strong, close-range
            return Lane>=MAXLANES-1;

        case ECombatantAttackType::Ranged:
            // Safer, slightly weaker
            return Lane>=MAXLANES-1;

        case ECombatantAttackType::Combi:
            // Flexible, tactical
            return true;

        case ECombatantAttackType::Healer:
            // No damage – healing handled elsewhere
            return false;

        default:
            // Should never happen, but safe fallback
            return false;
    }

}

float Combatant::Attack(float DefenseOfTarget)
{
    float NormalDmg= 5.f;
    switch (Difficulty)
    {
        case ECombatantDifficulty::Normal:
            return NormalDmg;

        case ECombatantDifficulty::Boss:
            return NormalDmg+5.f;

        case ECombatantDifficulty::GroupMob:
            return NormalDmg+6.f;

        case ECombatantDifficulty::GroupBoss:
            return NormalDmg+10.f;

        case ECombatantDifficulty::RaidMob:
            return NormalDmg+11.f;

        case ECombatantDifficulty::RaidBoss:
            return NormalDmg+15.f;

        default:
            return 1.f;
    }
}



void Combatant::InitFromMobTemplate(
    const std::string& mobClass,
    ECombatantAttackType attackType,
    ECombatantDifficulty difficulty,
    int maxHP,
    int maxEnergy,
    int maxMana,
    int baseDamage,
    int baseDefense
)
{
    MobClass = mobClass;
    AvatarId = mobClass;
    AttackType = attackType;
    Difficulty = difficulty;

    MaxHP = maxHP;     
    HP = maxHP;
    MaxEnergy = maxEnergy; 
    Energy = maxEnergy;
    MaxMana = maxMana; 
    Mana = maxMana;

    BaseDamage = baseDamage;
    BaseDefense = baseDefense;
}