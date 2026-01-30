// uistate.h
#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <optional>

#include "json.hpp"     // nlohmann::json
#include "combatant.h" // adjust include to your real Combatant header

class UIState
{
public:
    using json = nlohmann::json;

    UIState() = default;

    // State meta
    void SetTurn(int t) noexcept { Turn = t; }
    void SetSlots(int s) noexcept { Slots = s; }

    int GetTurn() const noexcept { return Turn; }
    int GetSlots() const noexcept { return Slots; }

    // Selection (optional)
    void SetSelectedMobId(std::optional<std::string> id) { SelectedMobId = std::move(id); }
    void SetSelectedPartyId(std::optional<std::string> id) { SelectedPartyId = std::move(id); }

    const std::optional<std::string>& GetSelectedMobId() const noexcept { return SelectedMobId; }
    const std::optional<std::string>& GetSelectedPartyId() const noexcept { return SelectedPartyId; }

    // Build full uiState json for the frontend
    json ToJson(const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Players,
                const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Mobs) const;

private:
    int Turn = 1;
    int Slots = 5;

    std::optional<std::string> SelectedMobId;
    std::optional<std::string> SelectedPartyId;

private:
    static json BuildDefaultLanes();

    static json BuildMobs(const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Mobs);
    static json BuildParty(const std::unordered_map<std::string, std::shared_ptr<Combatant>>& Players);

    static json MobToJson(const Combatant& c);
    static json PartyMemberToJson(const Combatant& c);

    // small helper
    static std::string GetStringOr(const json& j, const char* key, const std::string& fallback);
};
