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
#include <optional>
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
    int potions,
    int highestWave
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
                    highestWave= GREATEST(highestWave, ?),
                    StoreTime = CURRENT_TIMESTAMP
                WHERE CharacterId = ?
            )"
        )
    );

    stmt->setInt(1, level);
    stmt->setInt(2, xp);
    stmt->setInt(3, credits);
    stmt->setInt(4, potions);
    stmt->setInt(5, highestWave);
    stmt->setInt(6, characterId);

    DaraLog("DB", "Called to Save CharacterId: "+std::to_string(characterId));
    stmt->executeUpdate();
}

// =======================================================
// REMOVE CHARACTER
// (delete character slot)
// =======================================================
bool RemoveCharacter(std::string userKey, std::string pcharId)
{
    int characterId = std::stoi(pcharId); 
    if(characterId<=0){
        return false;
    }
    auto con = CreateDbConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        con->prepareStatement(
            "DELETE FROM Characters WHERE UserId= ? AND CharacterId = ?"
        )
    );

    stmt->setString(1, userKey);
    stmt->setInt(2, characterId);
    stmt->executeUpdate();
    return true;
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
                    highestWave,
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
        DaraLog("DEBUGDB", "Avatar: "+c.avatar);
        c.level          = rs->getInt("Level");
        c.xp             = rs->getInt("XP");
        c.credits        = rs->getInt("Credits");
        c.potions        = rs->getInt("Potions");
        c.highestWave = rs->getInt("highestWave");

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
    // ATTENTION This function does not seem to be used yet and highestWave is just 0 here
    auto dirty = CollectDirtyCharacters();
    for (auto& d : dirty){
            g_dbWorker.RequestSavePlayerCharacter(d.email, d.characterId, d.level, d.xp, d.credits, d.potions, 0);
    }
}


std::optional<Character> CreateCharacterForUserAndCache(
    const std::string& userKey,     // IMPORTANT: same key used by GetCharacters(userKey)
    const std::string& userEmail,
    const std::string& characterName,
    const std::string& characterClass,
    const std::string& avatar
)
{
    // 1) Create in DB (returns INT auto id)
    int newId = CreateCharacter(userKey, userEmail, characterName, characterClass, avatar);
    if (newId <= 0) {
        return std::nullopt; // DB insert failed
    }

    // 2) Build cache object
    Character ch;
    ch.characterId    = std::to_string(newId);
    ch.characterName  = characterName;
    ch.characterClass = characterClass;
    ch.avatar         = avatar;      // <-- don't forget, your struct likely has this
    ch.level          = 1;
    ch.xp             = 0;
    ch.credits        = 0;
    ch.potions        = 0;
    ch.createdAtIso   = NowIsoUtc();

    // 3) Store into cache (dedupe)
    {
        std::lock_guard<std::mutex> lock(g_charsMutex);
        auto& vec = g_charsByUser[userKey];

        auto exists = std::any_of(vec.begin(), vec.end(), [&](const Character& c){
            return c.characterId == ch.characterId;
        });
        if (!exists) vec.push_back(ch);
    }

    return ch; // return the created character to caller
}


/** BEGIN Leaderboards */
static std::vector<BestEntry> QueryTopN(sql::Connection* con, const std::string& metricCol, int topN, bool weekly)
{
    // metricCol must be one of: "highestWave", "Level", "Credits", "Potions"
    // We do RANK() over the chosen metric.
    // weekly uses StoreTime >= NOW() - INTERVAL 7 DAY

    std::string sqlQuery =
        "SELECT CharacterId, CharacterName, UserEmail, Avatar, val, rnk FROM ("
        "  SELECT "
        "    CharacterId, CharacterName, UserEmail, Avatar, "
        "    " + metricCol + " AS val, "
        "    RANK() OVER (ORDER BY " + metricCol + " DESC, CharacterId ASC) AS rnk "
        "  FROM Characters ";

    if (weekly)
        sqlQuery += "  WHERE StoreTime >= (NOW() - INTERVAL 7 DAY) ";

    sqlQuery +=
        ") t "
        "WHERE rnk <= ? "
        "ORDER BY rnk ASC, CharacterId ASC;";

    std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(sqlQuery));
    stmt->setInt(1, topN);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    std::vector<BestEntry> out;
    out.reserve(topN);

    while (rs->next())
    {
        BestEntry e;
        e.characterId   = rs->getInt("CharacterId");
        e.characterName = rs->getString("CharacterName");
        e.userEmail     = rs->getString("UserEmail");
        e.avatar        = rs->getString("Avatar");
        e.value         = rs->getInt("val");
        e.rank          = rs->getInt("rnk");
        out.push_back(std::move(e));
    }
    return out;
}



static std::vector<MyPlace> QueryMyPlaces(sql::Connection* con, const std::string& userEmail)
{
    // We compute ranks for EACH metric using window functions.
    // For weekly we rank only rows in the last 7 days.
    // For overall we rank over all rows.

    // Weekly ranks CTE
    const char* q =
        R"(
        WITH
        weekly AS (
            SELECT
                CharacterId,
                CharacterName,
                Avatar,
                RANK() OVER (ORDER BY highestWave DESC, CharacterId ASC) AS rWave,
                RANK() OVER (ORDER BY Level       DESC, CharacterId ASC) AS rLevel,
                RANK() OVER (ORDER BY Credits     DESC, CharacterId ASC) AS rCredits,
                RANK() OVER (ORDER BY Potions     DESC, CharacterId ASC) AS rPotions
            FROM Characters
            WHERE StoreTime >= (NOW() - INTERVAL 7 DAY)
        ),
        alltime AS (
            SELECT
                CharacterId,
                RANK() OVER (ORDER BY highestWave DESC, CharacterId ASC) AS rWave,
                RANK() OVER (ORDER BY Level       DESC, CharacterId ASC) AS rLevel,
                RANK() OVER (ORDER BY Credits     DESC, CharacterId ASC) AS rCredits,
                RANK() OVER (ORDER BY Potions     DESC, CharacterId ASC) AS rPotions
            FROM Characters
        )
        SELECT
            c.CharacterId,
            c.CharacterName,
            c.Avatar,

            COALESCE(w.rWave,    0) AS rankWaveWeek,
            COALESCE(w.rLevel,   0) AS rankLevelWeek,
            COALESCE(w.rCredits, 0) AS rankCreditsWeek,
            COALESCE(w.rPotions, 0) AS rankPotionsWeek,

            a.rWave    AS rankWaveAll,
            a.rLevel   AS rankLevelAll,
            a.rCredits AS rankCreditsAll,
            a.rPotions AS rankPotionsAll

        FROM Characters c
        LEFT JOIN weekly  w ON w.CharacterId = c.CharacterId
        JOIN      alltime a ON a.CharacterId = c.CharacterId
        WHERE c.UserEmail = ?
        ORDER BY c.CharacterId DESC;
        )";

    std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(q));
    stmt->setString(1, userEmail);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    std::vector<MyPlace> out;
    while (rs->next())
    {
        MyPlace p;
        p.characterId   = rs->getInt("CharacterId");
        p.characterName = rs->getString("CharacterName");
        p.avatar        = rs->getString("Avatar");

        p.rankWaveWeek    = rs->getInt("rankWaveWeek");
        p.rankLevelWeek   = rs->getInt("rankLevelWeek");
        p.rankCreditsWeek = rs->getInt("rankCreditsWeek");
        p.rankPotionsWeek = rs->getInt("rankPotionsWeek");

        p.rankWaveAll     = rs->getInt("rankWaveAll");
        p.rankLevelAll    = rs->getInt("rankLevelAll");
        p.rankCreditsAll  = rs->getInt("rankCreditsAll");
        p.rankPotionsAll  = rs->getInt("rankPotionsAll");

        out.push_back(std::move(p));
    }
    return out;
}


BestListsResult DbGetBestListsAndMyPlaces(const std::string& userEmail)
{
    auto con = CreateDbConnection();
    BestListsResult r;

    // weekly top 1-3
    r.topWaveWeek    = QueryTopN(con.get(), "highestWave", 3, true);
    r.topLevelWeek   = QueryTopN(con.get(), "Level",       3, true);
    r.topCreditsWeek = QueryTopN(con.get(), "Credits",     3, true);
    r.topPotionsWeek = QueryTopN(con.get(), "Potions",     3, true);

    // overall top 1-3
    r.topWaveAll     = QueryTopN(con.get(), "highestWave", 3, false);
    r.topLevelAll    = QueryTopN(con.get(), "Level",       3, false);
    r.topCreditsAll  = QueryTopN(con.get(), "Credits",     3, false);
    r.topPotionsAll  = QueryTopN(con.get(), "Potions",     3, false);

    // user ranks
    r.myPlaces = QueryMyPlaces(con.get(), userEmail);

    return r;
}

// Helper to convert your result structs to JSON.
// Put these static helpers near your route code (or in a cpp file).
static json BestEntryToJson(const BestEntry& e)
{
    return json{
        {"characterId",   e.characterId},
        {"characterName", e.characterName},
        {"userEmail",     e.userEmail},
        {"avatar",        e.avatar},
        {"value",         e.value},
        {"rank",          e.rank}
    };
}

static json MyPlaceToJson(const MyPlace& p)
{
    return json{
        {"characterId",   p.characterId},
        {"characterName", p.characterName},
        {"avatar",        p.avatar},

        {"rankWaveWeek",     p.rankWaveWeek},
        {"rankLevelWeek",    p.rankLevelWeek},
        {"rankCreditsWeek",  p.rankCreditsWeek},
        {"rankPotionsWeek",  p.rankPotionsWeek},

        {"rankWaveAll",      p.rankWaveAll},
        {"rankLevelAll",     p.rankLevelAll},
        {"rankCreditsAll",   p.rankCreditsAll},
        {"rankPotionsAll",   p.rankPotionsAll}
    };
}


json GetLeaderBoardsJson(const std::string eMail, int &status)
{
    try
    {
        // Use the right field name from your Session struct:
        // In your logs you used session.eMail (note capital M).
        const std::string userEmail = eMail;

        BestListsResult r = DbGetBestListsAndMyPlaces(userEmail);

        auto vecToJson = [](const std::vector<BestEntry>& v){
            json arr = json::array();
            for (const auto& e : v) arr.push_back(BestEntryToJson(e));
            return arr;
        };

        json out;
        out["status"] = "ok";

        out["weekly"] = {
            {"highestWave",    vecToJson(r.topWaveWeek)},
            {"highestLevel",   vecToJson(r.topLevelWeek)},
            {"highestCredits", vecToJson(r.topCreditsWeek)},
            {"highestPotions", vecToJson(r.topPotionsWeek)}
        };

        out["overall"] = {
            {"highestWave",    vecToJson(r.topWaveAll)},
            {"highestLevel",   vecToJson(r.topLevelAll)},
            {"highestCredits", vecToJson(r.topCreditsAll)},
            {"highestPotions", vecToJson(r.topPotionsAll)}
        };

        out["myPlaces"] = json::array();
        for (const auto& p : r.myPlaces) out["myPlaces"].push_back(MyPlaceToJson(p));

        status = 200;
        return out;
    }
    catch (const sql::SQLException& e)
    {
        DaraLog("LEADERBOARDS", std::string("SQLException: ") + e.what()
            + " code=" + std::to_string(e.getErrorCode())
            + " state=" + e.getSQLStateCStr());

        status = 500;
        return (json{
            {"status","error"},
            {"message","DB error while loading leaderboards"}
        }); 
    }
    catch (const std::exception& e)
    {
        DaraLog("LEADERBOARDS", std::string("Exception: ") + e.what());
        status = 500;
        return(json{
            {"status","error"},
            {"message","Server error while loading leaderboards"}
        });
    }
}