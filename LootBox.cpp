#include "LootBox.h"
#include "combatant.h"


LootBox::LootBox()
{
    bool isDmg = GetRandomFloat(0.f,1000.f)>900.f;

    if(isDmg){
        Dmg= GetRandomFloat(0.f,1000.f);
    }else{
        Heal=GetRandomSomeFloat(-1000.f, 1000.f);
        Energy=GetRandomSomeFloat(-1000.f, 1000.f);
        Mana=GetRandomSomeFloat(-1000.f, 1000.f);
        Credits= GetRandomSomeInt(-30,10);
        Potion= GetRandomSomeInt(-5,1);
    }


}

void LootBox::Debug()
{
    DaraLog("HEALER", "Heal: "+ std::to_string(Heal));
    DaraLog("HEALER", "Energy: "+ std::to_string(Energy));
    DaraLog("HEALER", "Mana: "+ std::to_string(Mana));
    DaraLog("HEALER", "Credits: "+ std::to_string(Credits));
}