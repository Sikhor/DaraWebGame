#pragma once
#include <string>
#include "DaraConfig.h"
#include "character.h"


void CommitCacheToDB();
void CacheSetCharactersForUser(const std::string& userKey, std::vector<Character> chars);
bool HasCharactersCachedForUser(const std::string& userKey);

std::optional<Character> CreateCharacterForUserAndCache(
    const std::string& userKey,     // IMPORTANT: same key used by GetCharacters(userKey)
    const std::string& userEmail,
    const std::string& characterName,
    const std::string& characterClass,
    const std::string& avatar
);

struct CharacterRecord
{
    int characterId = 0;
    std::string userId;
    std::string usereMail;
    std::string characterName;
    std::string characterClass;
    std::string avatar;
    int level = 0;
    int xp = 0;
    int credits = 0;
    int potions = 0;
    int highestWave= 0;
    std::string storeTime; // "YYYY-MM-DD HH:MM:SS"
};

[[maybe_unused]]
static Character RecordToCharacter(const CharacterRecord& r)
{
    Character c;
    c.characterId    = std::to_string(r.characterId); // IMPORTANT: your CharacterId is string currently
    c.characterName  = r.characterName;
    c.characterClass = r.characterClass;
    c.avatar         = r.avatar;
    c.level          = r.level;
    c.xp             = r.xp;
    c.credits        = r.credits;
    c.potions        = r.potions;
    c.createdAtIso   = r.storeTime; // or a proper ISO conversion
    return c;
}


std::vector<CharacterRecord> GetCharactersForUser(const std::string& userMail);

std::vector<Character> DbGetCharactersAsGameCharacters(const std::string& userEmail);

int CreateCharacter(const std::string& userId,
                    const std::string& userEmail,
                    const std::string& characterName,
                    const std::string& characterClass,
                    const std::string& avatar);

void UpdateCharacter(int characterId, int level, int xp, int credits, int potions, int highestWave);
bool RemoveCharacter(std::string userKey, std::string characterId);


/* LeaderBoards */
struct BestEntry
{
    int characterId = 0;
    std::string characterName;
    std::string userEmail;
    std::string avatar;
    int value = 0;              // the metric value for the category
    int rank = 0;               // 1..N (rank in that list)
};

struct MyPlace
{
    int characterId = 0;
    std::string characterName;
    std::string avatar;

    // weekly ranks
    int rankWaveWeek = 0;
    int rankLevelWeek = 0;
    int rankCreditsWeek = 0;
    int rankPotionsWeek = 0;

    // overall ranks
    int rankWaveAll = 0;
    int rankLevelAll = 0;
    int rankCreditsAll = 0;
    int rankPotionsAll = 0;
};

struct BestListsResult
{
    // top 1-3 weekly
    std::vector<BestEntry> topWaveWeek;
    std::vector<BestEntry> topLevelWeek;
    std::vector<BestEntry> topCreditsWeek;
    std::vector<BestEntry> topPotionsWeek;

    // top 1-3 overall
    std::vector<BestEntry> topWaveAll;
    std::vector<BestEntry> topLevelAll;
    std::vector<BestEntry> topCreditsAll;
    std::vector<BestEntry> topPotionsAll;

    // ranks for the user’s chars
    std::vector<MyPlace> myPlaces;
};

BestListsResult DbGetBestListsAndMyPlaces(const std::string& userEmail);
json GetLeaderBoardsJson(const std::string eMail, int &status);



