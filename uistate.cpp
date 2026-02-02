// uistate.cpp
#include "uistate.h"

#include <algorithm>
#include <vector>

UIState::json UIState::ToJson(
    const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Players,
    const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Mobs) const
{
    json ui;
    ui["turn"]  = Turn;
    ui["slots"] = Slots;

    ui["lanes"] = BuildDefaultLanes();

    ui["selectedMobId"]   = SelectedMobId ? json(*SelectedMobId) : json(nullptr);
    ui["selectedPartyId"] = SelectedPartyId ? json(*SelectedPartyId) : json(nullptr);

    ui["mobs"]  = BuildMobs(Mobs);
    ui["party"] = BuildParty(Players);

    return ui;
}

UIState::json UIState::BuildDefaultLanes()
{
    return json::array({
        { {"name","LONG RANGE"},  {"cls","long"},  {"range","8–10m"} },
        { {"name","MID RANGE"},   {"cls","mid"},   {"range","4–7m"}  },
        { {"name","MELEE RANGE"}, {"cls","melee"}, {"range","0–3m"}  }
    });
}

static std::vector<std::shared_ptr<Combatant>>
SortedCombatantsByName(const std::unordered_map<std::string, std::shared_ptr<Combatant>>& m)
{
    std::vector<std::shared_ptr<Combatant>> v;
    v.reserve(m.size());

    for (const auto& [k, ptr] : m)
    {
        if (ptr) v.push_back(ptr);
    }

    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b)
              {
                  return a->GetName() < b->GetName();
              });

    return v;
}

UIState::json UIState::BuildMobs(const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Mobs)
{
    json arr = json::array();

    const auto sorted = SortedCombatantsByName(Mobs);
    for (const auto& mobPtr : sorted)
    {
        arr.push_back(MobToJson(*mobPtr));
    }

    return arr;
}

UIState::json UIState::BuildParty(const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Players)
{
    json arr = json::array();

    const auto sorted = SortedCombatantsByName(Players);
    for (const auto& pPtr : sorted)
    {
        arr.push_back(PartyMemberToJson(*pPtr));
    }

    return arr;
}

UIState::json UIState::MobToJson(const Combatant& c)
{
    // Best practice: include lane/slot/maxHP in Combatant::ToJson()
    // If you don't yet, either add them there OR add getters (GetLane/GetSlot/GetMaxHP).
    const json j = c.ToJson();

    json out;
    out["id"] = GetStringOr(j, "name", c.GetInstanceId());
    out["displayName"] = GetStringOr(j, "displayname", c.GetName());

    // Lane/slot: from ToJson() if present, else fallback 0.
    // If you add getters, replace these two lines with:
    // out["lane"] = c.GetLane();
    // out["slot"] = c.GetSlot();
    // out["lane"] = j.value("lane", c.GetLane());
    // out["slot"] = j.value("slot", c.GetSlot());

    // center-of-cell mapping
    float x = (c.GetSlot() + 0.5f) / MAXSLOTS;
    float y = std::clamp((c.GetLane() + 0.5f) / MAXLANES, 0.1f,0.9f);

    // optional safety clamp
    x = std::clamp(x, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);

    out["x"] = x;
    out["y"] = y;

    // HP/max: your JS expects {hp, max}
    out["hp"]  = j.value("hp", c.GetHP());
    out["en"]  = j.value("en", c.GetEnergy());
    out["mn"]  = j.value("mn", c.GetMana());

    // try multiple common keys, fallback to current HP
    out["max"] = c.GetMaxHP();
    out["hpMax"] = c.GetMaxHP();
    out["enMax"] = c.GetMaxEnergy();
    out["mnMax"] = c.GetMaxMana();
    out["avatarId"] = c.GetAvatarId();
    out["difficulty"]= GetStringOr(j, "difficulty", c.GetDifficulty());
    out["attackType"]= GetStringOr(j, "attackType", c.GetAttackType());

    return out;
}

UIState::json UIState::PartyMemberToJson(const Combatant& c)
{
    const json j = c.ToJson();

    json out;
    out["id"]    = GetStringOr(j, "name", c.GetName());

    out["hp"]    = j.value("hp", c.GetHP());
    out["hpMax"] = j.value("maxHp", j.value("hpMax", c.GetMaxHP()));

    out["en"]    = j.value("energy", c.GetEnergy());
    out["enMax"] = j.value("maxEnergy", j.value("enMax", c.GetMaxEnergy()));

    out["mn"]    = j.value("mana", c.GetMana());
    out["mnMax"] = j.value("maxMana", j.value("mnMax", c.GetMaxMana()));

    out["potions"]= j.value("potions", c.GetPotionAmount());
    out["level"]= j.value("level", c.GetLevel());
    out["xp"]= j.value("xp", c.GetXP());
    out["credits"]= j.value("credits", c.GetCredits());

    // Optional "active" flag (current player highlight etc.)
    out["active"] = j.value("active", c.GetActive());

    // Optional avatar id if you support it (for mob images by id)
    out["avatarId"] = c.GetAvatarId();

    return out;
}

std::string UIState::GetStringOr(const json& j, const char* key, const std::string& fallback)
{
    if (j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return fallback;
}
