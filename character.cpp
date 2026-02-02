#include "character.h"
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <iostream>

std::unordered_map<std::string, std::vector<Character>> g_charsByUser;
std::mutex g_charsMutex;


static std::string NowIsoUtc()
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
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

// simple id generator; if you already have GenerateUUID() use that
static std::string GenerateCharacterId()
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

static json CharacterToJson(const Character& ch)
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

void SeedTestCharacters()
{
    std::lock_guard<std::mutex> lock(g_charsMutex);

    g_charsByUser.clear();

    // -----------------------------
    // User A
    // -----------------------------
    const std::string userA = "sikhor63@gmail.com";

    g_charsByUser[userA].push_back(Character{
        .characterId   = MakeTestId("char", 1),
        .characterName = "Alicia Storm",
        .characterClass= "Lyndarie",
        .level         = 7,
        .xp            = 340,
        .credits       = 1200,
        .potions       = 3,
        .createdAtIso  = NowIso()
    });

    g_charsByUser[userA].push_back(Character{
        .characterId   = MakeTestId("char", 2),
        .characterName = "Alicia Vex",
        .characterClass= "Mercenary",
        .level         = 3,
        .xp            = 90,
        .credits       = 400,
        .potions       = 1,
        .createdAtIso  = NowIso()
    });

    // -----------------------------
    // User B
    // -----------------------------
    const std::string userB = "bob@example.org";

    g_charsByUser[userB].push_back(Character{
        .characterId   = MakeTestId("char", 3),
        .characterName = "Borkhan",
        .characterClass= "Cyborg",
        .level         = 12,
        .xp            = 910,
        .credits       = 5200,
        .potions       = 6,
        .createdAtIso  = NowIso()
    });

    g_charsByUser[userB].push_back(Character{
        .characterId   = MakeTestId("char", 4),
        .characterName = "Borkhan-X",
        .characterClass         = "Tank",
        .level         = 5,
        .xp            = 210,
        .credits       = 900,
        .potions       = 2,
        .createdAtIso  = NowIso()
    });

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
