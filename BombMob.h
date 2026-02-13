#pragma once
#include "combatant.h"

class BombMob : public Combatant {
public:
    void MobAttack(CombatantPtr target) override {
    }

private:
    void Explode() {
        // deal AoE, mark self dead, add message, etc.
    }
};