#include "combatant.h"
#include "ServerOptions.h"
extern ServerOptions g_options;

float GetRandomFloat(float min, float max)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}
void ApplyRandomCost(float& current, float normal, float deviation)
{
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
    LastActive= std::chrono::steady_clock::now();
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

    LastActive= std::chrono::steady_clock::now();
}

Combatant::Combatant(){
    Combatant("unknown",ECombatantType::Player);
    LastActive= std::chrono::steady_clock::now();
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
    if(BurnedCounter>0){
        ApplyDamage(DAMAGE_VALUE_BURNED);
    }
    BurnedCounter-=1;
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
    BurnedCounter-=1;
    CheckStats();
}

void Combatant::CheckStats()
{
    HP= std::clamp(HP, 0.f, MaxHP);
    Mana= std::clamp(Mana, 0.f, MaxMana);
    Energy= std::clamp(Energy, 0.f, MaxEnergy);
    BaseDefense= std::clamp(BaseDefense, 0.f, 100000.f);
    BaseDamage= std::clamp(BaseDamage, 0.f, 100000.f);
    MezzCounter= std::clamp(MezzCounter, 0, MEZZTURNS);
    BurnedCounter= std::clamp(BurnedCounter, 0, BURNEDTURNS);
    DamageModifier= std::clamp(DamageModifier, 0.f, 1000000.f);
    DefenseModifier= std::clamp(DefenseModifier, 0.f, 1000000.f);
    if(BurnedCounter>0) AddCondition(ECondition::Burned);
    if(MezzCounter>0) AddCondition(ECondition::Mezzed);
}

std::string Combatant::GetName() const
{
    return Name;
}

void Combatant::MobAttack(CombatantPtr target)
{
    if(!IsAlive())return;
    if(MezzCounter>0)return;
    if(AttackType==ECombatantAttackType::Bomb) return;
    float dmg= 0.f;
    float nearRangeDmg;

    dmg = GetRandomDamage();
    // dmg + if the mob is near to the last lane
    nearRangeDmg= static_cast<float>(Lane/MAX_LANES)*dmg;
    dmg += nearRangeDmg;
    dmg-= target->GetCurrentDefense();
    target->ApplyDamage(dmg);
    if(DARA_DEBUG_COMBAT)DaraLog("COMBAT", GetName()+ " Lane: "+std::to_string(Lane)+" NearRngDmg: "+std::to_string(nearRangeDmg) +" attacks with: "+std::to_string(dmg));
}


float Combatant::AttackMelee(CombatantPtr target)
{
    float dmg= 0.f;
    if(!IsAlive())return dmg;
    if(target->GetLane()<= MAX_LANES-2)
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
    if(target->GetLane()<= MAX_LANES-5){
        //std::cout << "TOOOO FAR" << std::endl;
        //return 0.f;
    }

    if (Mana > SPELLCOST) {
        dmg = GetRandomDamage();
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        DaraLog("COMBAT", "Player: "+GetName()+ " attacks fireball with "+std::to_string(dmg)+" on a defense of: "+ std::to_string(target->GetCurrentDefense()));
        target->ReceiveBurned();
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
void Combatant::ReceiveBurned()
{
    BurnedCounter= BURNEDTURNS;
}


void Combatant::Heal(CombatantPtr target)
{
    if(!IsAlive())return;
    float healamount = 100.f+BaseDamage;
    if (Mana > SPELLCOST) {
        //healamount = GetRandomDamage();
        ApplyRandomCost(Mana, SPELLCOST, DEVIATION);
        target->ApplyHeal(healamount);
        DaraLog("COMBAT", GetName()+" heals "+target->GetName()+ " for "+std::to_string(healamount));
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

void Combatant::BuffAegolism()
{
    MaxHP= 4* MaxHP;
    HP= MaxHP;
    DamageModifier+= 4*DamageModifier;
    DefenseModifier+= 4*DefenseModifier;
    Difficulty= ECombatantDifficulty::GroupBoss;
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
    CheckStats();
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

void Combatant::CalcPos()
{
    float variation = 0.02f; // 2%
    float jittery = GetRandomFloat(-variation, variation);
    variation= 0.08f; //8%
    float jitterx = GetRandomFloat(-variation, variation);

    if(PosY<0.f) PosY = std::clamp((Lane+ 0.5f) / MAX_LANES, 0.2f,0.9f);
    if(PosX<0.f) PosX = (Slot + 0.5f) / MAX_SLOTS;

    if (!IsMezzed()) {
        if(g_options.noMobJitter==false){
            PosX += jitterx;
            PosY += jittery;
        }
    }

    // optional safety clamp
    PosY = std::clamp(PosY, 0.0f, 0.9f);
    PosX = std::clamp(PosX, 0.1f, 0.9f);
}
void Combatant::SetLane(int lane, int slot)
{
    Lane = lane;
    Slot = slot;
    CalcPos();
}
int Combatant::Move()
{
    Lane= static_cast<int>(CurrentField);
    PosY = std::clamp((Lane+ 0.5f) / MAX_LANES, 0.15f,0.8f);
    CalcPos();
    //DaraLog("MOVE", GetName()+" Lane: "+ std::to_string(Lane));
    return Lane;
}

bool Combatant::ShouldMove()
{
    if(MezzCounter>0)return false;

    CurrentField+=Speed;
    //return CurrentField>(Lane+1) && Lane<MAX_LANES;
    return Lane<MAX_LANES;
}

bool Combatant::ShouldAttack()
{
    switch (AttackType)
    {
        case ECombatantAttackType::Melee:
            // Strong, close-range
            return Lane>=MAX_LANES-1;

        case ECombatantAttackType::Ranged:
            // Safer, slightly weaker
            return true; // old one ! Lane>=MAX_LANES-1;

        case ECombatantAttackType::Combi:
            // Flexible, tactical
            return true;

        case ECombatantAttackType::Spider:
            // Safer, slightly weaker
            return true; // old one ! Lane>=MAX_LANES-1;

        case ECombatantAttackType::Insect:
            // Safer, slightly weaker
            return true; // old one ! Lane>=MAX_LANES-1;

        case ECombatantAttackType::Healer:
            // No damage – healing handled elsewhere
            return false;

            case ECombatantAttackType::Bomb:
            // No damage – only on explosion
            return false;

        default:
            // Should never happen, but safe fallback
            return false;
    }

}


void Combatant::InitFromMobTemplate(
    const std::string& mobClass,
    const std::string& avatarId,
    ECombatantAttackType attackType,
    ECombatantDifficulty difficulty,
    float speed,
    int maxHP,
    int maxEnergy,
    int maxMana,
    int baseDamage,
    int baseDefense
)
{
    MobClass = mobClass;
    AvatarId = avatarId;
    AttackType = attackType;
    Difficulty = difficulty;

    Speed= speed;

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
        case ECombatantAttackType::Spider:  return "Spider";
        case ECombatantAttackType::Insect:  return "Insect";
        case ECombatantAttackType::Healer:  return "Healer";
        default:  
          {
            DaraLog("MOBTEMPLATE", "Unknown AttackType") ;
            return "Unknown Atk";
          }
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
      << ", MezzCounter=" << BurnedCounter

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
      << ", BurnedCounter=" << BurnedCounter

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
    InitLevel(Level);
}

void Combatant::InitLevel(int level)
{
    Level= level;
    // Level 2 = Base *1.4  Level 3=Base*1.6 ...
    HP= STAT_BASE_MAX_HP*(1+Level*0.2);
    MaxHP= STAT_BASE_MAX_HP*(1+Level*0.2);
    Energy= STAT_BASE_MAX_ENERGY*(1+Level*0.2);
    MaxEnergy= STAT_BASE_MAX_ENERGY*(1+Level*0.2);
    Mana= STAT_BASE_MAX_MANA*(1+Level*0.2);
    MaxMana= STAT_BASE_MAX_MANA*(1+Level*0.2);
    BaseDamage=STAT_BASE_DAMAGE_PLAYER*(1+Level*0.2);
    BaseDefense=STAT_BASE_DEFENSE_PLAYER*(1+Level*0.2);

}

float Combatant::GetCurrentDefense() const
{
    float CurrentDefense= BaseDefense+DefenseModifier;;
    if(BurnedCounter>0){
        CurrentDefense-= DEBUFF_VALUE_BURNED;
        CurrentDefense= std::clamp(CurrentDefense, 0.f, 10000000.f);
    }
    if(DARA_DEBUG_MOBCOMBAT) DaraLog("COMBAT", GetName()+ " current Defense: "+std::to_string(CurrentDefense));
    return CurrentDefense;
}

json Combatant::GetConditionsJson() const 
{
    json arr = json::array();
    for (const auto& c : Conditions)
        arr.push_back(ToString(c));
    return arr;
}

void Combatant::AddCondition(ECondition c)
{
    if (c == ECondition::None) return;
    Conditions.insert(c);
}

float Combatant::GetX() const
{
    return PosX;
}

float Combatant::GetY() const
{
    return PosY;
}

void Combatant::MarkActive()
{
    LastActive= std::chrono::steady_clock::now();
    return;
}

bool Combatant::IsActive() const
{
    // 10min not marked active
    std::chrono::minutes t = std::chrono::minutes(10);
    std::chrono::steady_clock::time_point now= std::chrono::steady_clock::now();

    return (now-LastActive)< t;
}
bool Combatant::ShouldExplode()
{
    if(AttackType!=ECombatantAttackType::Bomb) return false;
    ExplodeCounter--;

    DaraLog("COMBAT", "ExplodeCounter: " + GetName()+ ":"+std::to_string(ExplodeCounter));

    return ExplodeCounter<1;
}

void Combatant::Explode(CombatantPtr target)
{
    if(AttackType!=ECombatantAttackType::Bomb) return;
    float dmg= 0.f;
    float nearRangeDmg;

    dmg = GetRandomDamage();
    // dmg + if the mob is near to the last lane
    nearRangeDmg= static_cast<float>(Lane/MAX_LANES)*dmg;
    dmg += nearRangeDmg;
    dmg-= target->GetCurrentDefense();
    target->ApplyDamage(dmg);
    //if(DARA_DEBUG_COMBAT)
    DaraLog("COMBAT", "Explosion "+GetName()+ " Lane: "+std::to_string(Lane)+" NearRngDmg: "+std::to_string(nearRangeDmg) +" attacks with: "+std::to_string(dmg));
    HP=0;
}
