#include "MobTemplateStore.h"
#include <fstream>

bool MobTemplateStore::LoadFromFile(const std::string& path, std::string* err)
{
    try {
        std::ifstream in(path);
        if (!in) {
            if (err) *err = "Cannot open mob template file: " + path;
            return false;
        }

        json root;
        in >> root;

        Templates.clear();
        Keys.clear();

        const auto& arr = root.at("mobs");
        if (!arr.is_array()) {
            if (err) *err = "`mobs` must be an array";
            return false;
        }

        for (const auto& m : arr) {
            MobTemplate t;
            t.id          = m.at("id").get<std::string>();
            t.displayName = m.at("displayName").get<std::string>();
            t.mobClass    = m.at("mobClass").get<std::string>();
            t.attackType  = ParseAttackType(m.at("attackType").get<std::string>());
            t.difficulty  = ParseDifficulty(m.at("difficulty").get<std::string>());
            t.speed       = m.at("speed").get<float>();
            t.wave        = m.at("wave").get<int>();
            t.maxHP       = m.at("maxHP").get<int>();
            t.maxEnergy   = m.at("maxEnergy").get<int>();
            t.maxMana     = m.at("maxMana").get<int>();
            t.baseDamage  = m.at("baseDamage").get<int>();
            t.baseDefense = m.at("baseDefense").get<int>();

            // store
            Templates[t.id] = t;
        }

        Keys.reserve(Templates.size());
        for (const auto& kv : Templates) Keys.push_back(kv.first);

        return true;
    }
    catch (const std::exception& e) {
        if (err) *err = std::string("Load mobs failed: ") + e.what();
        return false;
    }
}

bool MobTemplateStore::HasTemplate(const std::string& mobId) const
{
    return Templates.find(mobId) != Templates.end();
}

std::shared_ptr<Combatant> MobTemplateStore::CreateMobInstancePtr(const std::string& mobId, int lane, int slot) const
{
    Combatant tmp = CreateMobInstance(mobId, lane, slot);          // creates by value
    return std::make_shared<Combatant>(std::move(tmp)); // wraps into shared_ptr
}

Combatant MobTemplateStore::CreateMobInstance(const std::string& mobId, int lane, int slot) const
{
    const auto& t = Templates.at(mobId);

    Combatant mob(t.displayName, ECombatantType::Mob);
    mob.InitFromMobTemplate(
        t.mobClass,
        t.attackType,
        t.difficulty,
        t.speed,
        t.maxHP,
        t.maxEnergy,
        t.maxMana,
        t.baseDamage,
        t.baseDefense
    );
    mob.SetLane(lane,slot);
    return mob;
}

std::string MobTemplateStore::PickRandomMobId()
{
    if (Keys.empty()) return {};
    std::uniform_int_distribution<size_t> dist(0, Keys.size() - 1);
    return Keys[dist(rng)];
}

std::string MobTemplateStore::PickRandomMobIdForWave(int wave)
{
    std::vector<std::string> waveKeys;

    for (const auto& key : Keys)
    {
        auto it = Templates.find(key);
        if (it == Templates.end())
            continue;

        if (it->second.wave == wave && it->second.difficulty==ECombatantDifficulty::Normal)
        {
            waveKeys.push_back(key);
        }
    }

    if (waveKeys.empty())
        return {};

    std::uniform_int_distribution<size_t> dist(0, waveKeys.size() - 1);
    return waveKeys[dist(rng)];
}

std::string MobTemplateStore::PickRandomBossForWave(int wave)
{
    std::vector<std::string> matches;

    for (const auto& key : Keys)
    {
        auto it = Templates.find(key);
        if (it == Templates.end())
            continue;

        if (it->second.wave == wave && it->second.difficulty == ECombatantDifficulty::Boss)
        {
            matches.push_back(key);
        }
    }

    if (matches.empty())
        return {};

    std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
    return matches[dist(rng)];
}


ECombatantAttackType MobTemplateStore::ParseAttackType(const std::string& s)
{
    if (s == "Melee")  return ECombatantAttackType::Melee;
    if (s == "Ranged") return ECombatantAttackType::Ranged;
    if (s == "Combi")  return ECombatantAttackType::Combi;
    if (s == "Spider")  return ECombatantAttackType::Spider;
    if (s == "Insect")  return ECombatantAttackType::Insect;
    if (s == "Healer") return ECombatantAttackType::Healer;
    try
    {
        // somewhere deep inside
        throw std::runtime_error("Unknown attackType: " + s);
    }
    catch (const std::runtime_error& e)
    {
        DaraLog("ERROR", e.what());
    }
    return ECombatantAttackType::Melee;
}

ECombatantDifficulty MobTemplateStore::ParseDifficulty(const std::string& s)
{
    if (s == "Normal")    return ECombatantDifficulty::Normal;
    if (s == "Boss")      return ECombatantDifficulty::Boss;
    if (s == "GroupMob")  return ECombatantDifficulty::GroupMob;
    if (s == "GroupBoss") return ECombatantDifficulty::GroupBoss;
    if (s == "RaidMob")   return ECombatantDifficulty::RaidMob;
    if (s == "RaidBoss")  return ECombatantDifficulty::RaidBoss;
    try
    {
        // somewhere deep inside
        throw std::runtime_error("Unknown difficulty: " + s);
    }
    catch (const std::runtime_error& e)
    {
        DaraLog("ERROR", e.what());
    }
    return ECombatantDifficulty::Normal;
}
