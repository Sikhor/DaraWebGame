#include "character.h"
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <iostream>

std::unordered_map<std::string, std::vector<Character>> g_charsByUser;
std::mutex g_charsMutex;


std::string NowIsoUtc()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

// simple id generator; if you already have GenerateUUID() use that
std::string GenerateCharacterId()
{
    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    uint32_t a = dist(rng), b = dist(rng), c = dist(rng), d = dist(rng);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << a << "-"
        << std::setw(4) << ((b >> 16) & 0xFFFF) << "-"
        << std::setw(4) << (b & 0xFFFF) << "-"
        << std::setw(4) << ((c >> 16) & 0xFFFF) << "-"
        << std::setw(4) << (c & 0xFFFF)
        << std::setw(8) << d;
    return oss.str();
}

json CharacterToJson(const Character& ch)
{
    return json{
        {"characterId", ch.characterId},
        {"characterName", ch.characterName},
        {"class", ch.characterClass},
        {"level", ch.level},
        {"xp", ch.xp},
        {"credits", ch.credits},
        {"potions", ch.potions},
        {"createdAt", ch.createdAtIso}
    };
}

// Find character by id for a user; returns nullptr if not found.
Character* FindCharacterLocked(std::vector<Character>& vec, const std::string& characterId)
{
    for (auto& c : vec)
        if (c.characterId == characterId)
            return &c;
    return nullptr;
}

json SerializeSelectedCharacterLockedForUser(const std::string userKey, const std::string& selectedCharacterId)
{
    auto it = g_charsByUser.find(userKey);
    if (it == g_charsByUser.end())
        return "";

    const auto& vec = it->second;
    for (const auto& ch : vec)
    {
        if (ch.characterId==selectedCharacterId)
        return json{
            {"characterId",   ch.characterId},
            {"characterName", ch.characterName},
            {"class",         ch.characterClass},
            {"level",         ch.level},
            {"xp",            ch.xp},
            {"credits",       ch.credits},
            {"potions",       ch.potions},
            {"createdAt",     ch.createdAtIso}
        };
    }

    return "";
}

json SerializeCharactersForUser(const std::string& userKey)
{
    std::lock_guard<std::mutex> lock(g_charsMutex);
    return SerializeCharactersLockedForUser(userKey);
}

json SerializeCharactersLockedForUser(const std::string& userKey)
{
    json arr = json::array();

    auto it = g_charsByUser.find(userKey);
    if (it == g_charsByUser.end())
        return arr;

    const auto& vec = it->second;
    for (const auto& ch : vec)
    {
        arr.push_back(json{
            {"characterId",   ch.characterId},
            {"characterName", ch.characterName},
            {"class",         ch.characterClass},
            {"level",         ch.level},
            {"xp",            ch.xp},
            {"credits",       ch.credits},
            {"potions",       ch.potions},
            {"createdAt",     ch.createdAtIso}
        });
    }

    return arr;
}


// helper: ISO timestamp
static std::string NowIso()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// helper: fake uuid-like string (good enough for tests)
static std::string MakeTestId(const std::string& prefix, int n)
{
    return prefix + "-" + std::to_string(n);
}


void DebugDumpCharacters(const std::string& lookupUser,
                         const std::string& lookupCharacterId)
{
    std::lock_guard<std::mutex> lock(g_charsMutex);

    std::cout << "\n===== DEBUG DUMP g_charsByUser =====\n";
    std::cout << "Map size: " << g_charsByUser.size() << "\n";
    std::cout << "Looking up user key: [" << lookupUser << "]\n";
    std::cout << "Looking up characterId: [" << lookupCharacterId << "]\n\n";

    for (const auto& [user, vec] : g_charsByUser)
    {
        std::cout << "USER KEY: [" << user << "]  characters=" << vec.size() << "\n";

        for (const auto& ch : vec)
        {
            std::cout
                << "  - characterId=[" << ch.characterId << "]"
                << " name=[" << ch.characterName << "]"
                << " class=[" << ch.characterClass << "]"
                << " level=" << ch.level
                << "\n";
        }
    }

    std::cout << "===== END DEBUG DUMP =====\n\n";
}


void CacheApplyRewards(const std::string& userEmail,
                       const std::string& characterId,
                       int addXp, int addCredits, int addPotions)
{
    std::lock_guard<std::mutex> lock(g_charsMutex);
    auto it = g_charsByUser.find(userEmail);
    if (it == g_charsByUser.end()) return;
    auto* ch = FindCharacterLocked(it->second, characterId);
    if (!ch) return;

    ch->xp += addXp;
    ch->credits += addCredits;
    ch->potions += addPotions;
    ch->dirty = true;
    // set dirtySinceMs...
}


std::vector<DirtyToSave> CollectDirtyCharacters()
{
    std::vector<DirtyToSave> out;
    std::lock_guard<std::mutex> lock(g_charsMutex);

    for (auto& [email, vec] : g_charsByUser)
    {
        for (auto& ch : vec)
        {
            if (!ch.dirty) continue;
            ch.dirty = false;

            out.push_back({email, ch.characterId, ch.level, ch.xp, ch.credits, ch.potions});
        }
    }
    return out;
}
