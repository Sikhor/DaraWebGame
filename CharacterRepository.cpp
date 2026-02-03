#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>

#include <memory>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <vector>
#include "CharacterRepository.h"
#include "character.h"
#include "CharacterDbWorker.h"

extern CharacterDbWorker g_dbWorker;
// =============================
// DB Cache
// =============================

bool HasCharactersCachedForUser(const std::string& userKey)
{
    std::lock_guard<std::mutex> lock(g_charsMutex);
    return g_charsByUser.find(userKey) != g_charsByUser.end();
}

void CacheSetCharactersForUser(const std::string& userKey, std::vector<Character> chars)
{
    std::lock_guard<std::mutex> lock(g_charsMutex);
    g_charsByUser[userKey] = std::move(chars);
}

std::vector<Character> CacheCopyCharactersForUser(const std::string& userKey)
{
    std::lock_guard<std::mutex> lock(g_charsMutex);
    auto it = g_charsByUser.find(userKey);
    if (it == g_charsByUser.end()) return {};
    return it->second; // copy
}

std::vector<Character> DbGetCharactersAsGameCharacters(const std::string& userEmail)
{
    auto recs = GetCharactersForUser(userEmail);
    std::vector<Character> out;
    out.reserve(recs.size());
    for (const auto& r : recs) out.push_back(RecordToCharacter(r));
    return out;
}


// =============================
// DB CONFIG (env-based)
// =============================
static std::string GetEnvOrThrow(const char* key)
{
    const char* val = std::getenv(key);
    if (!val || !*val)
        throw std::runtime_error(std::string("Missing env var: ") + key);
    return val;
}

static std::unique_ptr<sql::Connection> CreateDbConnection()
{
    auto host = GetEnvOrThrow("DARA_DB_HOST");   // e.g. tcp://127.0.0.1:3306
    auto user = GetEnvOrThrow("DARA_DB_USER");
    auto pass = GetEnvOrThrow("DARA_DB_PASS");
    auto db   = GetEnvOrThrow("DARA_DB_NAME");   // e.g. darawebgame

    DaraLog("DB", "Before Driver connect host=" + host + " user=" + user + " db=" + db);

    try
    {
        sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

        // Optional: avoid hanging forever on network issues
        // (works depending on connector version; safe to try)
        // driver->setOption("OPT_CONNECT_TIMEOUT", "5");

        std::unique_ptr<sql::Connection> con(driver->connect(host, user, pass));

        DaraLog("DB", "After Driver connect (connected ok)");

        con->setSchema(db);
        con->setAutoCommit(true);

        return con;
    }
    catch (const sql::SQLException& e)
    {
        DaraLog("DB", std::string("SQLException: ") + e.what() +
                      " | MySQL error code=" + std::to_string(e.getErrorCode()) +
                      " | SQLState=" + e.getSQLStateCStr());
        throw; // rethrow so caller knows it failed
    }
    catch (const std::exception& e)
    {
        DaraLog("DB", std::string("std::exception: ") + e.what());
        throw;
    }
}

// =======================================================
// CREATE CHARACTER
// Returns new CharacterId
// =======================================================
int CreateCharacter(
    const std::string& userId,
    const std::string& userEmail,
    const std::string& characterName,
    const std::string& characterClass,
    const std::string& avatar
)
{
    DaraLog("DB", "Before Create DB Connection");
    auto con = CreateDbConnection();
    DaraLog("DB", "Before Create character");

    std::unique_ptr<sql::PreparedStatement> stmt(
        con->prepareStatement(
            R"(
                INSERT INTO Characters
                (UserId, UserEmail, CharacterName, CharacterClass, Avatar)
                VALUES (?, ?, ?, ?, ?)
            )"
        )
    );

    stmt->setString(1, userId);
    stmt->setString(2, userEmail);
    stmt->setString(3, characterName);
    stmt->setString(4, characterClass);
    stmt->setString(5, avatar);

    stmt->executeUpdate();

    DaraLog("DB", "After Create character");
    // Fetch auto-increment id
    std::unique_ptr<sql::PreparedStatement> idStmt(
        con->prepareStatement("SELECT LAST_INSERT_ID()")
    );

    std::unique_ptr<sql::ResultSet> rs(idStmt->executeQuery());
    if (!rs->next())
        throw std::runtime_error("Failed to retrieve CharacterId");

    return rs->getInt(1);
}

// =======================================================
// UPDATE CHARACTER
// (used after match, logout, periodic save)
// =======================================================
void UpdateCharacter(
    int characterId,
    int level,
    int xp,
    int credits,
    int potions
)
{
    auto con = CreateDbConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        con->prepareStatement(
            R"(
                UPDATE Characters
                SET Level = ?,
                    XP = ?,
                    Credits = ?,
                    Potions = ?,
                    StoreTime = CURRENT_TIMESTAMP
                WHERE CharacterId = ?
            )"
        )
    );

    stmt->setInt(1, level);
    stmt->setInt(2, xp);
    stmt->setInt(3, credits);
    stmt->setInt(4, potions);
    stmt->setInt(5, characterId);

    stmt->executeUpdate();
}

// =======================================================
// REMOVE CHARACTER
// (delete character slot)
// =======================================================
void RemoveCharacter(int characterId)
{
    auto con = CreateDbConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        con->prepareStatement(
            "DELETE FROM Characters WHERE CharacterId = ?"
        )
    );

    stmt->setInt(1, characterId);
    stmt->executeUpdate();
}


#include <vector>

// =======================================================
// GET CHARACTERS FOR USER (by email)
// Returns all character rows for that email, newest first
// =======================================================
std::vector<CharacterRecord> GetCharactersForUser(const std::string& userMail)
{
    auto con = CreateDbConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        con->prepareStatement(
            R"(
                SELECT
                    CharacterId,
                    UserId,
                    UserEmail,
                    CharacterName,
                    CharacterClass,
                    Avatar,
                    Level,
                    XP,
                    Credits,
                    Potions,
                    StoreTime
                FROM Characters
                WHERE UserEmail = ?
                ORDER BY CharacterId DESC
            )"
        )
    );

    stmt->setString(1, userMail);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    std::vector<CharacterRecord> out;
    out.reserve(8);

    while (rs->next())
    {
        CharacterRecord c{};
        c.characterId    = rs->getInt("CharacterId");
        c.userId         = rs->getString("UserId");
        c.usereMail      = rs->getString("UserEmail");
        c.characterName  = rs->getString("CharacterName");
        c.characterClass = rs->getString("CharacterClass");
        c.avatar         = rs->getString("Avatar");
        c.level          = rs->getInt("Level");
        c.xp             = rs->getInt("XP");
        c.credits        = rs->getInt("Credits");
        c.potions        = rs->getInt("Potions");

        // StoreTime can be NULL if you ever insert NULL explicitly; your schema defaults to CURRENT_TIMESTAMP,
        // so normally it's always present.
        // Convert to string (YYYY-MM-DD HH:MM:SS):
        try {
            c.storeTime = rs->getString("StoreTime");
        } catch (...) {
            c.storeTime = "";
        }

        out.push_back(std::move(c));
    }

    return out;
}

void CommitCacheToDB()
{
    auto dirty = CollectDirtyCharacters();
    for (auto& d : dirty){
            g_dbWorker.RequestSaveCharacter(d.email, d.characterId, d.level, d.xp, d.credits, d.potions);
    }
}


bool CreateCharacterForUserAndCache(
    const std::string& userId,
    const std::string& userEmail,
    const std::string& characterName,
    const std::string& characterClass,
    const std::string& avatar
)
{
    // 1) Create in DB (returns INT auto id)
    int newId = CreateCharacter(userId, userEmail, characterName, characterClass, avatar);

    // 2) Build cache object using that ID as string
    Character ch;
    ch.characterId    = std::to_string(newId);   // <-- "123"
    ch.characterName  = characterName;
    ch.characterClass = characterClass;
    ch.level          = 1;
    ch.xp             = 0;
    ch.credits        = 0;
    ch.potions        = 0;
    ch.createdAtIso   = NowIsoUtc();             // you already have this

    // 3) Store into cache
    {
        std::lock_guard<std::mutex> lock(g_charsMutex);
        g_charsByUser[userEmail].push_back(std::move(ch));
    }

    return true;
}
