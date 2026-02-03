#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <functional>

enum class EDbJobType
{
    LoadUserCharacters,
    SaveCharacter
};

struct DbJob
{
    EDbJobType type;
    std::string userEmail;

    // for SaveCharacter:
    std::string characterId;
    int level = 0;
    int xp = 0;
    int credits = 0;
    int potions = 0;

    std::function<void(bool ok)> done; 
};

class DbJobQueue
{
public:
    void Push(DbJob j);
    bool PopWait(DbJob& out);
    void Stop();

private:
    std::queue<DbJob> q_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};