// Sound effect ids. The first six match WeaponId so a weapon's id is also its firing sound.
#pragma once

enum Sfx {
  SFX_PULSE = 0, SFX_GATLING, SFX_ION, SFX_RAIL, SFX_PLASMA, SFX_MISSILE,
  SFX_SHIELD_HIT,   // shot absorbed by a shield
  SFX_HULL_HIT,     // light round on plating
  SFX_HULL_HEAVY,   // heavy round on plating
  SFX_PART_LOST,    // a part is torn off
  SFX_EXPLOSION,    // small blast (missile, asteroid)
  SFX_EXPLOSION_BIG,// a ship breaks up
  SFX_ROCK_HIT,
  SFX_COLLISION,
  SFX_SHIELD_DOWN,
  SFX_MISFIRE,
  SFX_ALARM,
  SFX_UI_MOVE,
  SFX_UI_SELECT,
  SFX_LAUNCH,
  SFX_COUNT
};
