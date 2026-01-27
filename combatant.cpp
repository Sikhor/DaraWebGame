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

Combatant::Combatant(const std::string& name, ECombatantType type, float hp, float energy, float mana)
{
    Id = GenerateUUID();
    Name = name;
    Type = type;
    HP = hp;
    Energy = energy;
    Mana = mana;
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

    if (HP > MAXHP) HP = MAXHP;
    if (Mana > MAXMANA) Mana = MAXMANA;
    if (Energy > MAXENERGY) Energy = MAXENERGY;
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
    if (HP < 0.f) HP = 0.f;
}

float Combatant::GetHPPercentage() const
{
    return (MaxHP > 0.f) ? (HP / MaxHP) * 100.f : 0.f;
}

float Combatant::GetManaPercentage() const
{
    return (MaxMana > 0.f) ? (Mana / MaxMana) * 100.f : 0.f;
}

float Combatant::GetEnergyPercentage() const
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
    j["HPPercentage"] = GetHPPercentage();
    j["EnergyPercentage"] = GetEnergyPercentage();
    j["ManaPercentage"] = GetManaPercentage();
    j["Conditions"] = json::array();
    for (const auto& condition : Conditions) {
        j["Conditions"].push_back(static_cast<int>(condition));
    }
    return j;
}




