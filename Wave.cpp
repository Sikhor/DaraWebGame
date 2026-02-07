#include <chrono>
#include "Wave.h"
#include "DaraConfig.h"
#include "combatant.h"

void Wave::ResetWaves() //resets everything that we can start at Wave 1 again
{
    WaveNumber=1;
    MobsSpawned=0;
    MobsToSpawn= DARA_MOBS_WAVE1;
    MobsToSpawn= std::clamp(MobsToSpawn, 1, DARA_MAX_MOBS_PERWAVE);
}

void Wave::Completed()
{
    WaveNumber++;
    MobsSpawned=0;
    MobsToSpawn=static_cast<int>(GetRandomFloat(4.f,float(DARA_MAX_MOBS_PERWAVE)));
    MobsToSpawn= std::clamp(MobsToSpawn, 1, DARA_MAX_MOBS_PERWAVE);
}
