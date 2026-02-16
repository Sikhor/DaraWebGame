#pragma once
#include <string>

class LootBox
{
public:
    float Dmg= 0.f;
    float Heal= 0.f;
    float Energy= 0.f;
    float Mana= 0.f;
    int Credits=0;
    int Potion=0;
    int XP=0;
    LootBox();
    void Debug();

};
