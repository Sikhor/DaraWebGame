#include <condition_variable>
#include <mutex>
#include <chrono>
#include "CharacterDbWorker.h"
#include "CharacterRepository.h"
#include "character.h"



// full implementation here (Run(), queue, etc.)
void CharacterDbWorker::Start()
{
    running_ = true;
    th_ = std::thread([this]{ Run(); });
}

void CharacterDbWorker::Stop()
{
    q_.Stop();
    if (th_.joinable()) th_.join();
    running_ = false;
}

void CharacterDbWorker::RequestLoadUser(const std::string& email)
{
    q_.Push(DbJob{EDbJobType::LoadUserCharacters, email});
}

void CharacterDbWorker::RequestSaveCharacter(const std::string& characterId,
                              int level, int xp, int credits, int potions, int highestWave)
{
    DbJob j;
    j.type = EDbJobType::SaveCharacter;
    j.characterId = characterId;
    j.level=level; j.xp=xp; j.credits=credits; j.potions=potions;
    j.highestWave= highestWave;
    q_.Push(std::move(j));

}
void CharacterDbWorker::RequestSavePlayerCharacter(const std::string& email,
                            const std::string& characterId,
                            int level,int xp,int credits,int potions, int highestWave)
{
    DbJob j;
    j.type = EDbJobType::SavePlayerCharacter;
    j.userEmail = email;
    j.characterId = characterId;
    j.level=level; j.xp=xp; j.credits=credits; j.potions=potions;
    j.highestWave= highestWave;
    q_.Push(std::move(j));
}

void CharacterDbWorker::Run()
{
    while (true)
    {
        DbJob job;
        if (!q_.PopWait(job)) break;

        try
        {
            if (job.type == EDbJobType::LoadUserCharacters)
            {
                auto chars = DbGetCharactersAsGameCharacters(job.userEmail);
                CacheSetCharactersForUser(job.userEmail, std::move(chars));
                DaraLog("DB", "Loaded characters for " + job.userEmail);
                if (job.done) job.done(true);
            }
            else if (job.type == EDbJobType::SavePlayerCharacter)
            {
                // You currently have UpdateCharacter(int characterId, ...)
                // but your cache uses string characterId. Convert:
                if (job.characterId.empty() || !std::isdigit((unsigned char)job.characterId[0]))
                    throw std::runtime_error("SavePlayerCharacter: characterId not numeric: " + job.characterId);
                int dbId = std::stoi(job.characterId); // works only if characterId is numeric string
                UpdateCharacter(dbId, job.level, job.xp, job.credits, job.potions, job.highestWave);
                DaraLog("DB", "Saved characterId=" + job.characterId);
            } else if (job.type == EDbJobType::SaveCharacter)
            {
                // You currently have UpdateCharacter(int characterId, ...)
                // but your cache uses string characterId. Convert:
                if (job.characterId.empty() || !std::isdigit((unsigned char)job.characterId[0]))
                    throw std::runtime_error("SaveCharacter: characterId not numeric: " + job.characterId);
                int dbId = std::stoi(job.characterId); // works only if characterId is numeric string
                UpdateCharacter(dbId, job.level, job.xp, job.credits, job.potions, job.highestWave);
                DaraLog("DB", "Saved characterId=" + job.characterId);
            }
        }
        catch (const std::exception& e)
        {
            DaraLog("DB", std::string("DB worker error: ") + e.what());
              if (job.done) job.done(false);
        }
    }
}

bool CharacterDbWorker::LoadUserBlocking(const std::string& email, int timeoutMs)
{
    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
    bool ok = false;

    DbJob j;
    j.type = EDbJobType::LoadUserCharacters;
    j.userEmail = email;
    j.done = [&](bool success)
    {
        std::lock_guard<std::mutex> lock(m);
        ok = success;
        finished = true;
        cv.notify_one();
    };

    q_.Push(std::move(j));

    std::unique_lock<std::mutex> lock(m);
    cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]{ return finished; });

    return finished && ok;
}