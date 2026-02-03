#pragma once

#include <string>
#include <thread>
#include <atomic>
#include "DbJobQueue.h"

class CharacterDbWorker
{
public:
    void Start();
    void Stop();

    void RequestLoadUser(const std::string& email);
    void RequestSaveCharacter(const std::string& email,
                              const std::string& characterId,
                              int level, int xp, int credits, int potions);
    bool LoadUserBlocking(const std::string& email, int timeoutMs);

private:
    void Run();
    DbJobQueue q_;
    std::thread th_;
    std::atomic<bool> running_{false};
};
