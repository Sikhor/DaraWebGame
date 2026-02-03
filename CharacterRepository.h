#pragma once
#include <string>
#include "DaraConfig.h"
#include "character.h"


void CommitCacheToDB();
void CacheSetCharactersForUser(const std::string& userKey, std::vector<Character> chars);
bool HasCharactersCachedForUser(const std::string& userKey);

bool CreateCharacterForUserAndCache(
    const std::string& userId,
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
    std::string storeTime; // "YYYY-MM-DD HH:MM:SS"
};

static Character RecordToCharacter(const CharacterRecord& r)
{
    Character c;
    c.characterId    = std::to_string(r.characterId); // IMPORTANT: your CharacterId is string currently
    c.characterName  = r.characterName;
    c.characterClass = r.characterClass;
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

void UpdateCharacter(int characterId, int level, int xp, int credits, int potions);
void RemoveCharacter(int characterId);



