#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <random>
#include "json.hpp"
#include "combatant.h"

class MobTemplateStore
{
public:
    using json = nlohmann::json;

    bool LoadFromFile(const std::string& path, std::string* err = nullptr);

    bool HasTemplate(const std::string& mobId) const;
    Combatant CreateMobInstance(const std::string& mobId, int lane, int slot) const;
    
    std::shared_ptr<Combatant> CreateMobInstancePtr(const std::string& mobId, int lane, int slot) const;

    // random picking
    bool Empty() const { return Keys.empty(); }
    std::string PickRandomMobId();
    std::string PickRandomMobIdForWave(int wave);
    std::string PickRandomBossForWave(int wave);

private:
    struct MobTemplate {
        std::string id;
        std::string displayName;
        std::string mobClass;
        ECombatantAttackType attackType;
        ECombatantDifficulty difficulty;
        float speed;
        int wave;
        int maxHP;
        int maxEnergy;
        int maxMana;
        int baseDamage;
        int baseDefense;
    };

    std::unordered_map<std::string, MobTemplate> Templates;
    std::vector<std::string> Keys;

    mutable std::mt19937 rng{ std::random_device{}() };

    static ECombatantAttackType ParseAttackType(const std::string& s);
    static ECombatantDifficulty ParseDifficulty(const std::string& s);
};
