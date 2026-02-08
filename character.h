#pragma once
#include <string>
#include <mutex>
#include "json.hpp"

using json = nlohmann::json;


struct DirtyToSave { std::string email; std::string characterId; int level,xp,credits,potions; };
std::vector<DirtyToSave> CollectDirtyCharacters();


void CacheApplyRewards(const std::string& userEmail,
                       const std::string& characterId,
                       int addXp, int addCredits, int addPotions);

// ===================== CHARACTER DATA (in-memory) =====================
// Replace this later with DB persistence.
struct Character
{
    std::string characterId;    // stable id (uuid-like)
    int characterIdDB;          // id from DB.. should atm 
    std::string characterName;  // shown to player (unique per user recommended)
    std::string characterClass;  // optional (e.g. "Lyndarie")
    std::string avatar;          // optional 
    int level = 1;
    int xp = 0;
    int credits = 0;
    int potions = 0;
    int highestWave= 0;
    std::string createdAtIso;   // optional
    bool dirty= false;
    uint64_t dirtySinceMs = 0; // or time_point
};
// userName -> characters
extern std::unordered_map<std::string, std::vector<Character>> g_charsByUser;
extern std::mutex g_charsMutex;

std::string NowIsoUtc();

// simple id generator; if you already have GenerateUUID() use that
std::string GenerateCharacterId();
json CharacterToJson(const Character& ch);

// Find character by id for a user; returns nullptr if not found.
Character* FindCharacterLocked(std::vector<Character>& vec, const std::string& characterId);

json SerializeSelectedCharacterLockedForUser(const std::string userKey, const std::string& selectedCharacterId);
json SerializeCharactersLockedForUser(const std::string& userKey);
json SerializeCharactersForUser(const std::string& userKey);

void SeedTestCharacters();

void DebugDumpCharacters(const std::string& lookupUser,
                         const std::string& lookupCharacterId);