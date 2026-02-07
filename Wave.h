#pragma once

class Wave
{
private:
    int WaveNumber=1;
    int MobsToSpawn= 2; //initially 5... will change per wave
    int MobsSpawned=0;
    int WaitTurns=5;

public:
    Wave(){};
    void ResetWaves(); //resets everything that we can start at Wave 1 again
    void SpawnedMob(){MobsSpawned++;}
    bool NeedToSpawnMob(){return MobsSpawned<=MobsToSpawn;}
    int GetMobsNotSpawnedYet(){return MobsToSpawn-MobsSpawned;}
    int GetWaveNumber(){return WaveNumber;}
    void Completed();
};