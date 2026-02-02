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

void Combatant::RegenTurnMob()
{
    DefenseModifier-=1.f;
    DamageModifier-=1.f;
    MezzCounter-=1;
    CheckStats();
}


void Combatant::RegenTurn()
{
    HP += GetRandomFloat(1.f, 4.f);
    Mana += GetRandomFloat(1.f, 4.f);
    Energy += GetRandomFloat(1.f, 4.f);

    DefenseModifier-=1.f;
    DamageModifier-=1.f;
    MezzCounter-=1;
    CheckStats();
}

void Combatant::CheckStats()
{
    HP= std::clamp(HP, 0.f, MAXHP);
    Mana= std::clamp(Mana, 0.f, MAXMANA);
    Energy= std::clamp(Energy, 0.f, MAXENERGY);
    BaseDefense= std::clamp(BaseDefense, 0.f, 100000.f);
    BaseDamage= std::clamp(BaseDamage, 0.f, 100000.f);
    MezzCounter= std::clamp(MezzCounter, 0, MEZZTURNS);
    DamageModifier= std::clamp(DamageModifier, 0.f, 1000000.f);
    DefenseModifier= std::clamp(DefenseModifier, 0.f, 1000000.f);
}

std::string Combatant::GetName() const
{
    return Name;
}

void Combatant::MobAttack(CombatantPtr target)
{
    if(!IsAlive())return;
    if(MezzCounter>0)return;
    float dmg= 0.f;

    dmg = GetRandomDamage();
    dmg-= target->GetCurrentDefense();
    target->ApplyDamage(dmg);
}


float Combatant::AttackMelee(CombatantPtr target)
{
    float dmg= 0.f;
    if(!IsAlive())return dmg;
    if(target->GetLane()<= MAXLANES-2)
    {
        //std::cout << "TOOOO FAR" << std::endl;
        return dmg;
    }

    if (Energy > MELEECOST) {
        dmg = GetRandomDamage();
        ApplyRandomCost(Energy, MELEECOST, DEVIATION);
        DaraLog("COMBAT", "Player: "+GetName()+ " attacks melee with "+std::to_string(dmg)+" on a defense of: "+ std::to_string(target->GetCurrentDefense()));
        dmg-= target->GetCurrentDefense();
        target->ApplyDamage(dmg);
    }
    return dmg;
}

float Combatant::AttackFireball(CombatantPtr target)
{
    float dmg= 0.f;

    if(!IsAlive())return dmg;
    if(target->GetLane()<= MAXLANES-5){
        //std::cout << "TOOOO FAR" << std::endl;
        //return 0.f;
    }

    if (Mana > SPELLCOST) {
        dmg = GetRandomDamage();
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        DaraLog("COMBAT", "Player: "+GetName()+ " attacks fireball with "+std::to_string(dmg)+" on a defense of: "+ std::to_string(target->GetCurrentDefense()));
        dmg-= target->GetCurrentDefense();
        target->ApplyDamage(dmg);
    }
    return dmg;
}

float Combatant::AttackShoot(CombatantPtr target)
{
    float dmg= 0.f;

    if(target->GetLane()>=2)
    {
        //std::cout << "TOOOO NEAR" << std::endl;
        //return dmg;
    }

    if(!IsAlive())return dmg;
    if (Energy > SPELLCOST) {
        dmg = GetRandomDamage();
        ApplyRandomCost(Energy, SPELLCOST, DEVIATION);
        DaraLog("COMBAT", "Player: "+GetName()+ " attacks shoot with "+std::to_string(dmg)+" on a defense of: "+ std::to_string(target->GetCurrentDefense()));
        dmg-= target->GetCurrentDefense();
        target->ApplyDamage(dmg);
    }
    return dmg;
}


float Combatant::AttackMezz(CombatantPtr target)
{
    if(!IsAlive())return 0.f;
    if (Mana > SPELLCOST) {
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        target->ReceiveMezz();
    }
    return static_cast<float>(MEZZTURNS);
}

void Combatant::ReceiveMezz()
{
    MezzCounter= MEZZTURNS;
}

void Combatant::Heal(CombatantPtr target)
{
    if(!IsAlive())return;
    float healamount = 0.f;
    if (Mana > SPELLCOST) {
        healamount = GetRandomDamage();
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        target->ApplyHeal(healamount);
    }

}

void Combatant::ReviveTarget(CombatantPtr target)
{
    if(!IsAlive())return;
    if (Mana > SPELLCOST) {
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        target->Revive();
    }
}

void Combatant::BuffDefense()
{
    if(!IsAlive())return;
    float spellcost= SPELLCOST*3;

    if (Mana > spellcost) {
        ApplyRandomCost(Mana, spellcost, DEVIATION);
        DefenseModifier+= 4*DefenseModifier;
    }
}
void Combatant::UsePotion()
{
    if(!IsAlive())return;
    if(PotionAmount<1)return;
    PotionAmount--;
    ApplyHeal(BaseDamage*50.f);
    Energy= MaxEnergy;
    Mana= MaxMana;
    DefenseModifier+= 2*BaseDefense;
}


void Combatant::ApplyDamage(float dmg)
{
    HP -= dmg;
    if (HP <= 0.f){ 
        HP = 0.f;
        AvatarId= DARA_DEAD_AVATAR_PLAYER;
    }
}

void Combatant::ApplyHeal(float amount)
{
    if(IsAlive()){
        HP += amount;
        CheckStats();
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
    if(MezzCounter>0)return false;

    CurrentField+=Speed;
    return CurrentField>(Lane+1) && Lane<MAXLANES && MezzCounter<1;
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

std::string Combatant::GetAttackType() const
{
    switch (AttackType)
    {
        case ECombatantAttackType::Ranged:  return "Ranged";
        case ECombatantAttackType::Melee:   return "Melee";
        case ECombatantAttackType::Combi:   return "Combi";
        case ECombatantAttackType::Healer:  return "Healer";
        default:                            return "Unknown";
    }
}

float Combatant::GetRandomDamage()
{
    float dmg= GetRandomFloat((BaseDamage+DamageModifier)*0.8f, (BaseDamage+DamageModifier)*1.2f);
    return std::clamp(dmg, 1.f,(BaseDamage+DamageModifier)*1.2f);
}

void Combatant::Debug()
{
    std::ostringstream oss;

    oss
      << "Combatant{"
      << "Id=" << Id
      << ", Name=" << Name
      << ", Type=" << ToString(Type)
      << ", Difficulty=" << ToString(Difficulty)
      << ", AttackType=" << ToString(AttackType)

      << ", Lane=" << Lane
      << ", Slot=" << Slot
      << ", Active=" << (Active ? "true" : "false")

      << ", AvatarId=" << AvatarId
      << ", MobClass=" << MobClass

      << ", HP=" << HP << "/" << MaxHP << " (" << HPPercentage << "%)"
      << ", Energy=" << Energy << "/" << MaxEnergy << " (" << EnergyPercentage << "%)"
      << ", Mana=" << Mana << "/" << MaxMana << " (" << ManaPercentage << "%)"

      << ", BaseDamage=" << BaseDamage
      << ", DamageMod=" << DamageModifier
      << ", CurrentDamage=" << (BaseDamage + DamageModifier)

      << ", BaseDefense=" << BaseDefense
      << ", DefenseMod=" << DefenseModifier
      << ", CurrentDefense=" << (BaseDefense + DefenseModifier)

      << ", Speed=" << Speed
      << ", CurrentField=" << CurrentField

      << ", SpellManaMin=" << SpellManaMin
      << ", MeleeManaMin=" << MeleeManaMin
      << ", PotionAmount=" << PotionAmount
      << ", MezzCounter=" << MezzCounter

      << ", Conditions=" << ConditionsToString(Conditions)
      << "}";

    // use your logger if you want:
    // DaraLog("COMBATANT", oss.str());
    std::cout << oss.str() << "\n";
}

void Combatant::DebugShort()
{
    std::ostringstream oss;

    oss
      << "[MOBSTATS]"
      << "  Name=" << Name
      << ", Type=" << ToString(Type)
      << ", Difficulty=" << ToString(Difficulty)
      << ", AttackType=" << ToString(AttackType)

      << ", Lane=" << Lane
      << ", Slot=" << Slot
      << ", Active=" << (Active ? "true" : "false")

      << ", AvatarId=" << AvatarId
      << ", MobClass=" << MobClass

      << ", HP=" << HP << "/" << MaxHP << " (" << HPPercentage << "%)"
      << ", Energy=" << Energy << "/" << MaxEnergy << " (" << EnergyPercentage << "%)"
      << ", Mana=" << Mana << "/" << MaxMana << " (" << ManaPercentage << "%)"

      << ", BaseDamage=" << BaseDamage
      << ", DamageMod=" << DamageModifier
      << ", CurrentDamage=" << (BaseDamage + DamageModifier)

      << ", BaseDefense=" << BaseDefense
      << ", DefenseMod=" << DefenseModifier
      << ", CurrentDefense=" << (BaseDefense + DefenseModifier)

      << ", Speed=" << Speed
      << ", CurrentField=" << CurrentField

      << ", SpellManaMin=" << SpellManaMin
      << ", MeleeManaMin=" << MeleeManaMin
      << ", PotionAmount=" << PotionAmount
      << ", MezzCounter=" << MezzCounter

      << ", Conditions=" << ConditionsToString(Conditions)
      << "}";

    // use your logger if you want:
    // DaraLog("COMBATANT", oss.str());
    std::cout << oss.str() << "\n";
}


void Combatant::AddXP(int amount)
{
    XP+=amount;
    if(XP>XPPERLEVEL){
        XP=0;
        LevelUp();
    }
}
void Combatant::LevelUp()
{
    Level+=1;
    BaseDamage+=2;
    BaseDefense+=2;
    MaxHP+=20;
    MaxEnergy+=20;
    MaxMana+=20;
}

void Combatant::InitLevel(int level)
{
    Level= level;
    HP= MAXHP*(1+Level*0.2);
    MaxHP= MAXHP*(1+Level*0.2);
    Energy= MAXENERGY*(1+Level*0.2);
    MaxEnergy= MAXENERGY*(1+Level*0.2);
    Mana= MAXMANA*(1+Level*0.2);
    MaxMana= MAXMANA*(1+Level*0.2);
    BaseDamage=STAT_BASEDAMAGE_PLAYER+(Level*2);
    BaseDefense=STAT_BASEDEFENSE_PLAYER+(Level*2);

}