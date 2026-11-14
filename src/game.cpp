// Game flow (menu -> hangar -> combat -> result), input, chase camera, HUD.
#include "game.h"
#include "audio.h"
#include "render_world.h"
#include "ui.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

const Color kHud{0.45f, 0.9f, 1.0f, 1.0f}, kDim{0.3f, 0.55f, 0.65f, 1.0f}, kAmber{1.0f, 0.75f, 0.2f, 1.0f},
    kRed{1.0f, 0.3f, 0.25f, 1.0f}, kGreen{0.4f, 1.0f, 0.6f, 1.0f}, kWhite{1, 1, 1, 1}, kPanel{0.02f, 0.05f, 0.08f, 0.72f};

Color healthColor(float f) {
  if (f > 0.66f) return {0.3f, 0.95f, 0.55f, 1};
  if (f > 0.33f) return {1.0f, 0.8f, 0.2f, 1};
  return {1.0f, 0.3f, 0.2f, 1};
}
std::string fmt(const char* f, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, f);
  std::vsnprintf(buf, sizeof(buf), f, ap);
  va_end(ap);
  return buf;
}

enum State { ST_MENU, ST_HANGAR, ST_PLAY, ST_PAUSE, ST_END };

class Game {
 public:
  GameOptions opt;
  GLFWwindow* win = nullptr;
  Renderer renderer;
  FrameData fd;
  UI ui;
  World world;
  Camera cam;
  State state = ST_MENU;
  Audio audio;
  int difficulty = 1;
  int songOwner = 0; // 1 = menu / hangar piece, 2 = combat piece
  uint32_t lobbySeed = 0;
  float alarmT = 0, toastT = 0, mutedVolume = -1;
  std::string toast;
  Rng sfxRng{99};

  // input
  bool keys[GLFW_KEY_LAST + 1]{}, pressed[GLFW_KEY_LAST + 1]{};
  bool mouse[8]{}, mousePressed[8]{};
  double mx = 0, my = 0, lastMx = 0, lastMy = 0;
  float mdx = 0, mdy = 0;
  bool firstMouse = true;

  // play state
  float stickX = 0, stickY = 0;
  bool assist = true, invertY = false, showHelp = false;
  Quat camRot;
  Vec3 camPos;
  float fov = 62, orbit = 0, playTime = 0, uiTime = 0;
  int menuSel = 0, pauseSel = 0;
  uint32_t seedCounter = 0;

  // hangar
  int loadout[2] = {W_PULSE, W_RAIL};
  int slot = 0, sectorPref = -1;
  Ship hangarShip, parkedShip;
  float hangarOrbit = 0.55f;
  bool windowed = true;
  int winX = 0, winY = 0, winW = 1600, winH = 900;

  // ------------------------------------------------------------------ setup
  uint32_t newSeed() {
    if (opt.seed) return opt.seed + seedCounter++;
    auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (uint32_t)(t ^ (t >> 21)) + seedCounter++ * 7919u;
  }
  void setCursor(bool captured) {
    glfwSetInputMode(win, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    firstMouse = true;
  }
  void lobbyMusic() {
    if (songOwner != 1) {
      if (!lobbySeed) lobbySeed = newSeed() | 1u;
      audio.newSong(lobbySeed);
      songOwner = 1;
    }
    audio.setEngine(-1, 0);
  }
  void enterMenu() {
    state = ST_MENU;
    world.difficulty = difficulty;
    world.generate(newSeed(), opt.sector, loadout[0], loadout[1], false);
    setCursor(false);
    lobbyMusic();
  }
  void enterHangar() {
    state = ST_HANGAR;
    hangarShip.build(PAL_PLAYER, loadout[0], loadout[1], 1);
    if (parkedShip.parts.empty()) parkedShip.build(PAL_PLAYER, W_ION, W_MISSILE, 7);
    setCursor(false);
    lobbyMusic();
  }
  void startGame() {
    world.autopilot = opt.autopilot;
    world.difficulty = difficulty;
    uint32_t seed = newSeed();
    world.generate(seed, opt.sector >= 0 ? opt.sector : sectorPref, loadout[0], loadout[1], true);
    audio.newSong(seed * 31u + 7u); // every sector gets its own piece of music
    songOwner = 2;
    lobbySeed = 0;
    audio.play(SFX_LAUNCH, 1.0f);
    alarmT = 0;
    state = ST_PLAY;
    stickX = stickY = 0;
    playTime = 0;
    assist = true;
    camRot = world.player().rot;
    camPos = world.player().pos + rotate(camRot, Vec3(0, 5.6f, 21));
    setCursor(true);
  }

  // ------------------------------------------------------------------ helpers
  float vmx() const { return (float)mx * ui.W / (float)std::max(1, renderer.width()); }
  float vmy() const { return (float)my * ui.H / (float)std::max(1, renderer.height()); }
  bool hover(float x, float y, float w, float h) const { return vmx() >= x && vmx() <= x + w && vmy() >= y && vmy() <= y + h; }
  bool key(int k) const { return keys[k]; }
  bool hit(int k) const { return pressed[k]; }

  bool button(float x, float y, float w, float h, const std::string& label, bool selected, float size = 3) {
    bool hov = hover(x, y, w, h);
    Color c = selected || hov ? kHud : kDim;
    ui.rect(x, y, w, h, selected || hov ? Color{0.08f, 0.2f, 0.28f, 0.85f} : kPanel);
    ui.frame(x, y, w, h, 2, c);
    if (selected) ui.rect(x, y, 6, h, kAmber);
    ui.textC(x + w * 0.5f, y + (h - 7 * size) * 0.5f, size, selected || hov ? kWhite : c, label);
    return hov;
  }

  // ------------------------------------------------------------------ sound
  void showToast(const std::string& s) { toast = s; toastT = 2.0f; }
  void cycleDifficulty(int dir) {
    difficulty = (difficulty + dir + DIFFICULTY_COUNT) % DIFFICULTY_COUNT;
    audio.play(SFX_UI_MOVE);
  }
  // Turn the simulation's sound events into voices: quieter with distance, panned by direction.
  void playWorldSounds() {
    const ShipEntity& p = world.player();
    int perSfx[SFX_COUNT] = {0};
    for (const SoundEvent& e : world.sounds) {
      if (e.sfx < 0 || e.sfx >= SFX_COUNT || ++perSfx[e.sfx] > 3) continue;
      Vec3 d = e.pos - cam.pos;
      float dist = length(d), gain, pan;
      if (e.ship == 0) {
        gain = e.gain * (e.sfx >= SFX_SHIELD_HIT ? 1.2f : 1.0f); // what happens to your own ship is loudest
        pan = clampf(dot(e.pos - p.pos, cam.right) / 8.0f, -0.5f, 0.5f);
      } else {
        gain = e.gain * 0.9f / (1.0f + dist / 220.0f);
        pan = clampf(dot(d, cam.right) / std::max(dist, 1.0f), -1.0f, 1.0f) * 0.8f;
      }
      audio.play(e.sfx, gain, pan, sfxRng.range(0.9f, 1.02f));
    }
    world.sounds.clear();
  }
  void combatAudio(float dt) {
    const ShipEntity& p = world.player();
    playWorldSounds();
    audio.setEngine(p.alive ? p.mainOut : -1.0f, p.alive && p.boosting ? 1.0f : 0.0f);
    float I;
    if (world.result == 1 || world.enemiesAlive == 0) I = 0.25f;
    else if (!p.alive) I = 0.08f;
    else {
      float nearest = 1e9f;
      for (const ShipEntity& e : world.ships)
        if (e.alive && e.team != p.team) nearest = std::min(nearest, length(e.pos - p.pos));
      float hull = (p.ship.stats.hullIntegrity - kDeathIntegrity) / (1.0f - kDeathIntegrity);
      I = 0.48f + 0.3f * (1.0f - smoothstepf(450.0f, 1500.0f, nearest)) + (p.shield < p.shieldMax * 0.3f ? 0.12f : 0.0f) + (hull < 0.5f ? 0.1f : 0.0f);
      alarmT -= dt;
      if (hull < 0.3f && alarmT <= 0) { audio.play(SFX_ALARM); alarmT = 1.5f; }
    }
    audio.setIntensity(I);
  }

  // ------------------------------------------------------------------ menu
  void updateMenu(float dt) {
    static const char* items[] = {"ENTER HANGAR", "QUICK LAUNCH", "QUIT"};
    world.update(std::min(dt, 0.033f));
    orbit += dt * 0.12f;
    ShipEntity& p = world.player();
    p.in = Controls();
    cam.rot = fromEuler(-0.18f, orbit, 0);
    cam.pos = p.pos + rotate(cam.rot, Vec3(0, -1.5f, 34));
    cam.fovY = radians(55);

    audio.setIntensity(0.2f);
    int prevSel = menuSel;
    if (hit(GLFW_KEY_DOWN) || hit(GLFW_KEY_S)) menuSel = (menuSel + 1) % 3;
    if (hit(GLFW_KEY_UP) || hit(GLFW_KEY_W)) menuSel = (menuSel + 2) % 3;
    if (hit(GLFW_KEY_LEFT) || hit(GLFW_KEY_A)) cycleDifficulty(-1);
    if (hit(GLFW_KEY_RIGHT) || hit(GLFW_KEY_D)) cycleDifficulty(1);
    int act = -1;
    if (hit(GLFW_KEY_ENTER) || hit(GLFW_KEY_SPACE)) act = menuSel;

    beginFrame(false);
    renderWorld(fd, world, cam);
    float cx = ui.W * 0.5f;
    ui.textC(cx, 150, 13, kHud, "HULLBREAKER");
    ui.textC(cx, 262, 2.5f, kDim, "EVERY PART COUNTS  -  NEWTONIAN SPACE COMBAT");
    for (int i = 0; i < 3; ++i) {
      float y = 500 + i * 70.0f;
      bool hov = button(cx - 200, y, 400, 56, items[i], menuSel == i);
      if (hov && (mdx != 0 || mdy != 0)) menuSel = i;
      if (hov && mousePressed[0]) act = i;
    }
    { // difficulty selector
      const Difficulty& D = DIFFICULTY[difficulty];
      float y = 724;
      ui.rect(cx - 330, y, 660, 78, kPanel);
      ui.frame(cx - 330, y, 660, 78, 2, kDim);
      ui.textC(cx, y + 10, 1.8f, kDim, "DIFFICULTY");
      static const Color dc[4] = {kGreen, kHud, kAmber, kRed};
      ui.textC(cx, y + 30, 3, dc[difficulty], D.name);
      ui.textC(cx, y + 58, 1.6f, kDim, D.blurb);
      bool hl = hover(cx - 330, y, 110, 78), hr = hover(cx + 220, y, 110, 78);
      ui.textC(cx - 275, y + 26, 4, hl ? kWhite : kHud, "<");
      ui.textC(cx + 275, y + 26, 4, hr ? kWhite : kHud, ">");
      if (hl && mousePressed[0]) cycleDifficulty(-1);
      if (hr && mousePressed[0]) cycleDifficulty(1);
    }
    ui.textC(cx, 824, 1.8f, kDim, "UP / DOWN SELECT    LEFT / RIGHT DIFFICULTY    ENTER CONFIRM    F11 FULLSCREEN");
    ui.textC(cx, 850, 1.8f, kDim, "F5 / F6 MUSIC VOLUME    F7 / F8 EFFECTS VOLUME    M MUTE MUSIC");
    if (menuSel != prevSel) audio.play(SFX_UI_MOVE);
    if (act >= 0) audio.play(SFX_UI_SELECT);
    if (act == 0) enterHangar();
    else if (act == 1) startGame();
    else if (act == 2 || hit(GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(win, GLFW_TRUE);
  }

  // ------------------------------------------------------------------ hangar
  void statBar(float x, float y, const char* label, float frac, const std::string& value, Color c) {
    ui.text(x, y, 2, kDim, label);
    ui.bar(x + 130, y + 1, 220, 12, frac, c, Color{0.1f, 0.15f, 0.2f, 0.9f});
    ui.text(x + 362, y, 2, kWhite, value);
  }
  void updateHangar(float dt) {
    audio.setIntensity(0.36f);
    int prevSlot = slot, prevSector = sectorPref;
    bool rebuilt = false;
    auto equip = [&](int w) {
      if (loadout[slot] != w) { loadout[slot] = w; rebuilt = true; }
    };
    if (hit(GLFW_KEY_TAB) || hit(GLFW_KEY_LEFT) || hit(GLFW_KEY_RIGHT) || hit(GLFW_KEY_A) || hit(GLFW_KEY_D)) slot ^= 1;
    if (hit(GLFW_KEY_DOWN) || hit(GLFW_KEY_S)) equip((loadout[slot] + 1) % W_COUNT);
    if (hit(GLFW_KEY_UP) || hit(GLFW_KEY_W)) equip((loadout[slot] + W_COUNT - 1) % W_COUNT);
    if (hit(GLFW_KEY_G)) sectorPref = sectorPref + 1 >= SEC_COUNT ? -1 : sectorPref + 1;
    if (hit(GLFW_KEY_F)) cycleDifficulty(1);
    bool launch = hit(GLFW_KEY_ENTER) || hit(GLFW_KEY_SPACE);
    bool back = hit(GLFW_KEY_ESCAPE);

    // ---- the camera circles the ship slowly; drag with the right mouse button to look around
    if (opt.orbit > -100) hangarOrbit = opt.orbit;
    hangarOrbit += dt * 0.09f;
    if (mouse[1]) hangarOrbit -= mdx * 0.006f + dt * 0.09f;
    {
      Vec3 focus(0, 3.1f, 0);
      Vec3 eye = focus + Vec3(std::sin(hangarOrbit) * 24.0f, 3.4f + 1.5f * std::sin(uiTime * 0.13f), std::cos(hangarOrbit) * 24.0f);
      Quat look = lookRotation(focus - eye);
      Vec3 shift = rotate(look, Vec3(1, 0, 0)) * 6.2f; // keeps the ship left of the loadout panel
      cam.pos = eye + shift;
      cam.rot = look;
      cam.fovY = radians(46);
    }
    beginFrame(true);
    renderHangar(fd, hangarShip, &parkedShip, uiTime);

    // ---- UI
    ui.rect(26, 22, 660, 112, kPanel);
    ui.rect(26, 766, 660, 108, kPanel);
    ui.text(40, 34, 5, kHud, "HANGAR");
    ui.text(40, 84, 2, kDim, "VX-9 KESTREL STRIKE FIGHTER  -  BAY 3");
    const ShipStats& st = hangarShip.stats;
    ui.text(40, 780, 2, kDim, fmt("PARTS  %d", (int)hangarShip.parts.size()));
    ui.text(40, 802, 2, kDim, fmt("MASS   %.1f T", st.mass / 1000.0f));
    ui.text(40, 824, 2, kDim, fmt("ACCEL  %.0f M/S2    CRUISE %.0f M/S    BOOST %.0f M/S", kMainAccel, kMaxSpeed, kBoostSpeed));
    ui.text(40, 846, 2, kDim, "SHIELD 140    EVERY PART CAN BE SHOT OFF");

    float px = ui.W - 600, pw = 560;
    ui.rect(px - 20, 20, pw + 40, 860, Color{0.02f, 0.05f, 0.08f, 0.93f});
    ui.frame(px - 20, 20, pw + 40, 860, 2, kDim);
    ui.text(px, 40, 3, kHud, "WEAPON LOADOUT");
    for (int s = 0; s < 2; ++s) {
      float x = px + s * (pw * 0.5f + 5), w = pw * 0.5f - 5;
      bool hov = hover(x, 78, w, 76);
      bool sel = slot == s;
      ui.rect(x, 78, w, 76, sel ? Color{0.08f, 0.2f, 0.28f, 0.9f} : Color{0.03f, 0.07f, 0.1f, 0.9f});
      ui.frame(x, 78, w, 76, 2, sel ? kAmber : (hov ? kHud : kDim));
      ui.text(x + 12, 88, 2, sel ? kAmber : kDim, s == 0 ? "SLOT 1 - PORT  [LMB]" : "SLOT 2 - STBD  [RMB]");
      ui.text(x + 12, 116, 2.5f, kWhite, WEAPONS[loadout[s]].name);
      if (hov && mousePressed[0]) slot = s;
    }
    float maxDmg = 0, maxRof = 0, maxSpd = 0, maxRange = 0;
    for (auto& w : WEAPONS) {
      maxDmg = std::max(maxDmg, w.damage); maxRof = std::max(maxRof, 1.0f / w.reload);
      maxSpd = std::max(maxSpd, w.speed); maxRange = std::max(maxRange, std::min(w.speed * w.life, 4000.0f));
    }
    for (int i = 0; i < W_COUNT; ++i) {
      const WeaponDef& w = WEAPONS[i];
      float y = 172 + i * 56.0f;
      bool hov = hover(px, y, pw, 50);
      bool sel = loadout[slot] == i;
      ui.rect(px, y, pw, 50, sel ? Color{0.08f, 0.2f, 0.28f, 0.9f} : (hov ? Color{0.05f, 0.12f, 0.17f, 0.9f} : Color{0.03f, 0.07f, 0.1f, 0.9f}));
      ui.frame(px, y, pw, 50, 2, sel ? kHud : (hov ? kDim : Color{0.15f, 0.25f, 0.3f, 1}));
      ui.rect(px + 2, y + 2, 8, 46, Color{w.color.x, w.color.y, w.color.z, 1});
      ui.text(px + 24, y + 7, 2.5f, sel ? kWhite : kHud, w.name);
      ui.text(px + 24, y + 31, 1.6f, w.heavy ? kAmber : kGreen, w.heavy ? "HEAVY" : "LIGHT");
      ui.text(px + 100, y + 31, 1.6f, kDim, fmt("DMG %.0f   RELOAD %.2fS   DPS %.0f", w.damage, w.reload, weaponDps(w)));
      for (int s = 0; s < 2; ++s)
        if (loadout[s] == i) {
          float bx = px + pw - 80 + s * 36;
          ui.rect(bx, y + 12, 28, 26, kAmber);
          ui.textC(bx + 14, y + 17, 2.4f, Color{0, 0, 0, 1}, s == 0 ? "1" : "2");
        }
      if (hov && mousePressed[0]) equip(i);
    }
    const WeaponDef& w = WEAPONS[loadout[slot]];
    float dy = 516;
    ui.text(px, dy, 2.5f, kHud, w.name);
    ui.textR(px + pw, dy + 2, 2, w.heavy ? kAmber : kGreen, w.heavy ? "HEAVY WEAPON" : "LIGHT WEAPON");
    Color wc{w.color.x, w.color.y, w.color.z, 1};
    statBar(px, dy + 34, "DAMAGE", w.damage / maxDmg, fmt("%.0f", w.damage), wc);
    statBar(px, dy + 58, "FIRE RATE", (1.0f / w.reload) / maxRof, fmt("%.1f /S", 1.0f / w.reload), wc);
    statBar(px, dy + 82, "VELOCITY", w.speed / maxSpd, fmt("%.0f M/S", w.speed), wc);
    statBar(px, dy + 106, "RANGE", std::min(w.speed * w.life, 4000.0f) / maxRange, fmt("%.1f KM", w.speed * w.life / 1000.0f), wc);
    std::string extra = fmt("ENERGY %.1f/SHOT   VS SHIELD X%.1f   VS HULL X%.2f", w.energy, w.shieldMul, w.hullMul);
    ui.text(px, dy + 134, 1.6f, kDim, extra);
    ui.textWrap(px, dy + 156, pw, 1.8f, kWhite.withA(0.85f), w.desc);

    ui.rect(px, 734, pw, 2, kDim.withA(0.5f));
    static const Color dc[4] = {kGreen, kHud, kAmber, kRed};
    bool hd = hover(px, 742, pw, 40);
    ui.text(px, 746, 2, hd ? kWhite : kDim, "DIFFICULTY [F]:");
    ui.text(px + 190, 746, 2, dc[difficulty], DIFFICULTY[difficulty].name);
    ui.text(px, 767, 1.5f, kDim, DIFFICULTY[difficulty].blurb);
    if (hd && mousePressed[0]) cycleDifficulty(1);
    bool hs = hover(px, 786, pw, 24);
    ui.text(px, 790, 2, hs ? kWhite : kDim, std::string("DESTINATION [G]: ") + (sectorPref < 0 ? "RANDOM SECTOR" : SECTOR_NAMES[sectorPref]));
    if (hs && mousePressed[0]) sectorPref = sectorPref + 1 >= SEC_COUNT ? -1 : sectorPref + 1;
    if (button(px, 820, pw - 150, 48, "LAUNCH  [ENTER]", true) && mousePressed[0]) launch = true;
    if (button(px + pw - 140, 820, 140, 48, "BACK", false, 2.5f) && mousePressed[0]) back = true;
    ui.text(40, 110, 1.8f, kDim, "UP/DOWN: WEAPON   LEFT/RIGHT: SLOT   RIGHT MOUSE: LOOK AROUND");
    if (slot != prevSlot || sectorPref != prevSector) audio.play(SFX_UI_MOVE);
    if (rebuilt || back) audio.play(SFX_UI_SELECT);

    if (rebuilt) hangarShip.build(PAL_PLAYER, loadout[0], loadout[1], 1);
    if (launch) startGame();
    else if (back) enterMenu();
  }

  // ------------------------------------------------------------------ combat
  void readControls() {
    ShipEntity& p = world.player();
    if (hit(GLFW_KEY_Z) || hit(GLFW_KEY_V)) {
      assist = !assist;
      world.say(assist ? "FLIGHT ASSIST ON" : "FLIGHT ASSIST OFF - NEWTONIAN DRIFT", Vec3(0.5f, 0.9f, 1.0f), 3);
    }
    if (hit(GLFW_KEY_I)) invertY = !invertY;
    if (hit(GLFW_KEY_TAB) || hit(GLFW_KEY_T)) world.cycleTarget(1);
    if (hit(GLFW_KEY_R)) world.targetNearest();
    if (hit(GLFW_KEY_F1) || hit(GLFW_KEY_H)) showHelp = !showHelp;
    if (mousePressed[2] || hit(GLFW_KEY_F)) stickX = stickY = 0;

    // virtual joystick: the mouse moves a cursor away from centre, the offset is the turn rate
    const float sens = 1.0f / 260.0f;
    stickX += mdx * sens;
    stickY += mdy * sens;
    float l = std::sqrt(stickX * stickX + stickY * stickY);
    if (l > 1) { stickX /= l; stickY /= l; }
    auto curve = [](float v) {
      float a = std::fabs(v);
      if (a < 0.04f) return 0.0f;
      a = (a - 0.04f) / 0.96f;
      return (v < 0 ? -1.0f : 1.0f) * a * (0.55f + 0.45f * a);
    };
    if (world.autopilot || !p.alive) return;
    Controls c;
    c.thrust = (key(GLFW_KEY_W) ? 1.0f : 0.0f) - (key(GLFW_KEY_S) ? 1.0f : 0.0f);
    c.strafeX = (key(GLFW_KEY_D) ? 1.0f : 0.0f) - (key(GLFW_KEY_A) ? 1.0f : 0.0f);
    c.strafeY = (key(GLFW_KEY_SPACE) ? 1.0f : 0.0f) - ((key(GLFW_KEY_LEFT_CONTROL) || key(GLFW_KEY_C)) ? 1.0f : 0.0f);
    c.roll = (key(GLFW_KEY_E) ? 1.0f : 0.0f) - (key(GLFW_KEY_Q) ? 1.0f : 0.0f);
    c.yaw = clampf(curve(stickX) + (key(GLFW_KEY_RIGHT) ? 1.0f : 0.0f) - (key(GLFW_KEY_LEFT) ? 1.0f : 0.0f), -1, 1);
    c.pitch = clampf((invertY ? 1.0f : -1.0f) * curve(stickY) + (key(GLFW_KEY_UP) ? 1.0f : 0.0f) - (key(GLFW_KEY_DOWN) ? 1.0f : 0.0f), -1, 1);
    c.boost = key(GLFW_KEY_LEFT_SHIFT);
    c.brake = key(GLFW_KEY_X);
    c.assist = assist;
    c.fire[0] = mouse[0] || key(GLFW_KEY_J);
    c.fire[1] = mouse[1] || key(GLFW_KEY_K);
    p.in = c;
  }

  void simulate(float dt) {
    int n = std::max(1, std::min(8, (int)std::ceil(dt * 120.0f)));
    for (int i = 0; i < n; ++i) world.update(dt / (float)n);
  }

  void updateCamera(float dt) {
    ShipEntity& p = world.player();
    if (p.alive) {
      camRot = nlerp(camRot, p.rot, 1.0f - std::exp(-dt * 6.5f));
      Vec3 want = p.pos + rotate(camRot, Vec3(0, 5.6f, 21.0f));
      // follow the ship's velocity exactly, lag behind its accelerations
      camPos += p.vel * dt;
      camPos = lerpv(camPos, want, 1.0f - std::exp(-dt * 9.0f));
      float sp = length(p.vel);
      float wantFov = 62.0f + 9.0f * clampf((sp - kMaxSpeed) / (kBoostSpeed - kMaxSpeed), 0.0f, 1.0f) + (p.boosting ? 4.0f : 0.0f);
      fov = lerpf(fov, wantFov, 1.0f - std::exp(-dt * 3.0f));
      cam.rot = camRot * axisAngle(Vec3(1, 0, 0), radians(-8.0f));
      cam.pos = camPos;
      if (opt.camYaw != 0) { // testing: look at the ship from the side
        Quat q = camRot * axisAngle(Vec3(0, 1, 0), radians(opt.camYaw));
        cam.rot = q * axisAngle(Vec3(1, 0, 0), radians(-8.0f));
        cam.pos = p.pos + rotate(q, Vec3(0, 5.6f, 24.0f));
      }
    } else {
      orbit += dt * 0.3f;
      cam.rot = fromEuler(-0.25f, orbit, 0);
      cam.pos = world.lastPlayerPos + rotate(cam.rot, Vec3(0, 0, 55));
    }
    if (world.shake > 0.01f) {
      Rng r((uint64_t)(world.time * 1000.0f));
      cam.pos += r.inSphere() * (world.shake * 0.35f);
    }
    cam.fovY = radians(fov);
  }

  void updatePlay(float dt) {
    if (hit(GLFW_KEY_ESCAPE)) { state = ST_PAUSE; pauseSel = 0; setCursor(false); audio.setEngine(-1, 0); audio.play(SFX_UI_SELECT); return; }
    readControls();
    simulate(dt);
    playTime += dt;
    updateCamera(dt);
    combatAudio(dt);
    beginFrame(false);
    renderWorld(fd, world, cam);
    drawHud();
    if (world.result != 0) { state = ST_END; setCursor(false); }
  }

  void updatePause() {
    static const char* items[] = {"RESUME", "RESTART (NEW SECTOR)", "HANGAR", "MAIN MENU"};
    int prevSel = pauseSel;
    audio.setIntensity(0.3f);
    if (hit(GLFW_KEY_DOWN) || hit(GLFW_KEY_S)) pauseSel = (pauseSel + 1) % 4;
    if (hit(GLFW_KEY_UP) || hit(GLFW_KEY_W)) pauseSel = (pauseSel + 3) % 4;
    int act = -1;
    if (hit(GLFW_KEY_ENTER) || hit(GLFW_KEY_SPACE)) act = pauseSel;
    if (hit(GLFW_KEY_ESCAPE)) act = 0;
    beginFrame(false);
    renderWorld(fd, world, cam);
    ui.rect(0, 0, ui.W, ui.H, Color{0, 0.02f, 0.04f, 0.6f});
    float cx = ui.W * 0.5f;
    ui.textC(cx, 200, 7, kHud, "PAUSED");
    for (int i = 0; i < 4; ++i) {
      bool hov = button(cx - 220, 340 + i * 72.0f, 440, 56, items[i], pauseSel == i);
      if (hov && (mdx != 0 || mdy != 0)) pauseSel = i;
      if (hov && mousePressed[0]) act = i;
    }
    drawHelp(660);
    ui.textC(cx, 792, 1.8f, kDim, std::string("MUSIC, COMPOSED LIVE FOR THIS SECTOR: ") + audio.songInfo());
    ui.textC(cx, 816, 1.8f, kDim, "F5 / F6 MUSIC VOLUME    F7 / F8 EFFECTS VOLUME    M MUTE MUSIC");
    if (pauseSel != prevSel) audio.play(SFX_UI_MOVE);
    if (act >= 0) audio.play(SFX_UI_SELECT);
    if (act == 0) { state = ST_PLAY; setCursor(true); }
    else if (act == 1) startGame();
    else if (act == 2) enterHangar();
    else if (act == 3) enterMenu();
  }

  void updateEnd(float dt) {
    simulate(dt);
    updateCamera(dt);
    combatAudio(dt);
    beginFrame(false);
    renderWorld(fd, world, cam);
    bool win = world.result == 1;
    const ShipEntity& p = world.player();
    float cx = ui.W * 0.5f;
    ui.rect(0, 150, ui.W, 520, Color{0, 0.02f, 0.04f, 0.72f});

    ui.rect(0, 150, ui.W, 3, win ? kGreen : kRed);
    ui.rect(0, 667, ui.W, 3, win ? kGreen : kRed);
    ui.textC(cx, 185, 8, win ? kGreen : kRed, win ? "SECTOR CLEARED" : "SHIP DESTROYED");
    ui.textC(cx, 265, 2.5f, kDim, win ? "ALL HOSTILES ELIMINATED" : "YOUR SHIP WAS LOST WITH ALL HANDS");
    int kills = world.enemiesTotal - world.enemiesAlive;
    float acc = p.shotsFired > 0 ? 100.0f * (float)p.shotsHit / (float)p.shotsFired : 0;
    float y = 318;
    auto row = [&](const char* k, const std::string& v) {
      ui.textR(cx - 20, y, 2.5f, kDim, k);
      ui.text(cx + 20, y, 2.5f, kWhite, v);
      y += 32;
    };
    row("SECTOR", world.sector.name + "  " + SECTOR_NAMES[world.sector.type]);
    row("DIFFICULTY", DIFFICULTY[world.difficulty].name);
    row("HOSTILES DESTROYED", fmt("%d / %d", kills, world.enemiesTotal));
    row("TIME", fmt("%d:%02d", (int)playTime / 60, (int)playTime % 60));
    row("ACCURACY", fmt("%.0f%%  (%d SHOTS)", acc, p.shotsFired));
    row("PARTS LOST", fmt("%d / %d", p.partsLost, (int)p.ship.parts.size()));
    row("HULL INTEGRITY", fmt("%.0f%%", p.alive ? clampf((p.ship.stats.hullIntegrity - kDeathIntegrity) / (1.0f - kDeathIntegrity), 0.0f, 1.0f) * 100.0f : 0.0f));
    int act = -1;
    if (button(cx - 470, 590, 300, 54, "PLAY AGAIN [R]", false, 2.5f) && mousePressed[0]) act = 0;
    if (button(cx - 150, 590, 300, 54, "HANGAR [ENTER]", true, 2.5f) && mousePressed[0]) act = 1;
    if (button(cx + 170, 590, 300, 54, "MENU [ESC]", false, 2.5f) && mousePressed[0]) act = 2;
    if (hit(GLFW_KEY_R)) act = 0;
    if (hit(GLFW_KEY_ENTER)) act = 1;
    if (hit(GLFW_KEY_ESCAPE)) act = 2;
    if (act >= 0) audio.play(SFX_UI_SELECT);
    if (act == 0) startGame();
    else if (act == 1) enterHangar();
    else if (act == 2) enterMenu();
  }

  // ------------------------------------------------------------------ HUD
  void drawHelp(float y) {
    static const char* lines[] = {
        "MOUSE  STEER (PITCH / YAW)      Q / E  ROLL            W / S  THRUST FORWARD / REVERSE",
        "A / D  STRAFE                   SPACE / CTRL  UP / DOWN    SHIFT  BOOST     X  BRAKE",
        "LMB  FIRE SLOT 1    RMB  FIRE SLOT 2    TAB  NEXT TARGET    R  NEAREST TARGET",
        "Z  FLIGHT ASSIST ON / OFF       F  CENTRE STICK     I  INVERT PITCH     ESC  PAUSE"};
    ui.rect(ui.W * 0.5f - 560, y - 14, 1120, 124, kPanel);
    for (int i = 0; i < 4; ++i) ui.textC(ui.W * 0.5f, y + i * 26.0f, 2, kHud, lines[i]);
  }

  void drawSchematic(const Ship& ship, float x, float y) {
    const float k = 12.0f, w = 250, h = 276;
    ui.rect(x, y, w, h, kPanel);
    ui.frame(x, y, w, h, 2, kDim);
    float cx = x + w * 0.5f, cy = y + 150;
    // draw low parts first so upper parts sit on top
    std::vector<int> order;
    for (int i = 0; i < (int)ship.parts.size(); ++i)
      if (!ship.parts[i].internal) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return ship.parts[a].pos.y < ship.parts[b].pos.y; });
    for (int i : order) {
      const Part& p = ship.parts[i];
      Vec3 ax = absv(rotate(p.rot, Vec3(1, 0, 0))), ay = absv(rotate(p.rot, Vec3(0, 1, 0))), az = absv(rotate(p.rot, Vec3(0, 0, 1)));
      float ex = (ax.x * p.scale.x + ay.x * p.scale.y + az.x * p.scale.z) * 0.5f;
      float ez = (ax.z * p.scale.x + ay.z * p.scale.y + az.z * p.scale.z) * 0.5f;
      float px = cx + (p.pos.x - ex) * k, py = cy + (p.pos.z - ez) * k;
      float pw = std::max(ex * 2 * k, 2.0f), ph = std::max(ez * 2 * k, 2.0f);
      if (!p.alive) {
        ui.rect(px, py, pw, ph, Color{0.35f, 0.04f, 0.03f, 0.35f});
        continue;
      }
      Color c = healthColor(p.frac());
      if (p.flash > 0) c = kWhite;
      ui.rect(px, py, pw, ph, c.withA(0.55f));
      ui.frame(px, py, pw, ph, 1, c.withA(0.9f) * 0.6f);
    }
    ui.text(x + 8, y + 8, 1.6f, kDim, "DAMAGE CONTROL");
  }

  void drawHud() {
    const ShipEntity& p = world.player();
    const ShipStats& st = p.ship.stats;
    float W = ui.W, H = ui.H, cx = W * 0.5f;

    // ---- hit vignette
    if (p.hitFlash > 0) {
      float a = clampf(p.hitFlash * 2.0f, 0.0f, 0.5f);
      Color c{1, 0.1f, 0.05f, a};
      ui.rect(0, 0, W, 14, c); ui.rect(0, H - 14, W, 14, c); ui.rect(0, 14, 14, H - 28, c); ui.rect(W - 14, 14, 14, H - 28, c);
    }

    // ---- messages
    float my0 = 30;
    for (const Message& m : world.messages) {
      float a = clampf(m.ttl, 0.0f, 1.0f);
      ui.text(30, my0, 2, Color{m.color.x, m.color.y, m.color.z, a}, m.text);
      my0 += 22;
    }
    ui.textC(cx, 24, 2.5f, kHud, fmt("HOSTILES  %d / %d", world.enemiesAlive, world.enemiesTotal));
    ui.textC(cx, 50, 1.6f, kDim, world.sector.name + "  -  " + SECTOR_NAMES[world.sector.type]);
    ui.textC(cx, 68, 1.6f, kDim, std::string("DIFFICULTY  ") + DIFFICULTY[world.difficulty].name);
    if (world.autopilot) ui.textC(cx, 86, 1.6f, kAmber, "AUTOPILOT ENGAGED");

    if (!p.alive) return;

    // ---- targets
    Vec3 fwd = rotate(p.rot, Vec3(0, 0, -1));
    for (const ShipEntity& e : world.ships) {
      if (!e.alive || e.team == p.team) continue;
      bool sel = e.id == p.target;
      float sx, sy;
      bool front = cam.project(e.pos, W, H, sx, sy);
      float dist = length(e.pos - p.pos);
      bool on = front && sx > 20 && sx < W - 20 && sy > 20 && sy < H - 20;
      Color c = sel ? kRed : kAmber.withA(0.8f);
      if (on) {
        float half = clampf(14.0f * 60.0f / std::max(dist, 30.0f) * 6.0f, 14.0f, 120.0f);
        if (sel) {
          ui.brackets(sx, sy, half, half * 0.45f, 2.5f, c);
          ui.textC(sx, sy - half - 40, 1.8f, c, e.callsign);
          ui.textC(sx, sy - half - 22, 1.8f, c, fmt("%.0f M", dist));
          float bw = 90;
          ui.bar(sx - bw * 0.5f, sy + half + 8, bw, 5, e.shield / std::max(e.shieldMax, 1.0f), Color{0.3f, 0.6f, 1, 1}, Color{0.05f, 0.1f, 0.2f, 0.8f});
          ui.bar(sx - bw * 0.5f, sy + half + 16, bw, 5, (e.ship.stats.hullIntegrity - kDeathIntegrity) / (1.0f - kDeathIntegrity), healthColor((e.ship.stats.hullIntegrity - kDeathIntegrity) / (1.0f - kDeathIntegrity)), Color{0.2f, 0.05f, 0.05f, 0.8f});
          const ShipStats& es = e.ship.stats;
          std::string dmg;
          if (es.engine[0] <= 0 || es.engine[1] <= 0) dmg += "ENG ";
          if (es.weapon[0] <= 0 || es.weapon[1] <= 0) dmg += "WPN ";
          if (es.authority[2] < 0.6f) dmg += "WING ";
          if (!dmg.empty()) ui.textC(sx, sy + half + 26, 1.6f, kAmber, dmg + "DAMAGED");
        } else {
          float d = 9;
          ui.line(sx - d, sy, sx, sy - d, 2, c); ui.line(sx, sy - d, sx + d, sy, 2, c);
          ui.line(sx + d, sy, sx, sy + d, 2, c); ui.line(sx, sy + d, sx - d, sy, 2, c);
          ui.textC(sx, sy + 14, 1.5f, c, fmt("%.0f", dist));
        }
      } else {
        Vec3 d = e.pos - cam.pos;
        float ax = dot(d, cam.right), ay = -dot(d, cam.up), l = std::sqrt(ax * ax + ay * ay);
        if (l < 1e-3f) { ax = 0; ay = -1; l = 1; }
        ax /= l; ay /= l;
        float r = H * 0.40f;
        float px = cx + ax * r, py = H * 0.5f + ay * r;
        float bx = -ay, by = ax, s = sel ? 16.0f : 11.0f;
        ui.line(px + ax * s, py + ay * s, px - ax * s * 0.5f + bx * s * 0.7f, py - ay * s * 0.5f + by * s * 0.7f, 3, c);
        ui.line(px + ax * s, py + ay * s, px - ax * s * 0.5f - bx * s * 0.7f, py - ay * s * 0.5f - by * s * 0.7f, 3, c);
        if (sel) ui.textC(px - ax * 34, py - ay * 34 - 6, 1.6f, c, fmt("%.0f M", dist));
      }
      // lead indicator: where to shoot to hit the selected target
      if (sel && st.sensors > 0.01f) {
        int k = st.weapon[0] > 0 ? 0 : 1;
        float ps = WEAPONS[p.ship.weapon[k]].speed;
        float t = dist / ps;
        float lx, ly;
        if (cam.project(e.pos + (e.vel - p.vel) * t, W, H, lx, ly)) {
          ui.ring(lx, ly, 11, 2, kGreen);
          ui.disc(lx, ly, 2, kGreen);
        }
      }
    }

    // ---- gun cross and steering cursor
    float gx, gy;
    if (cam.project(p.pos + fwd * kConvergence, W, H, gx, gy)) {
      Color c = kHud;
      ui.line(gx - 22, gy, gx - 8, gy, 2, c); ui.line(gx + 8, gy, gx + 22, gy, 2, c);
      ui.line(gx, gy - 22, gx, gy - 8, 2, c); ui.line(gx, gy + 8, gx, gy + 22, 2, c);
      ui.disc(gx, gy, 1.8f, c);
      float sxp = gx + stickX * 150, syp = gy + stickY * 150;
      ui.ring(gx, gy, 150, 1, kDim.withA(0.18f));
      if (std::fabs(stickX) + std::fabs(stickY) > 0.03f) {
        ui.line(gx, gy, sxp, syp, 1, kHud.withA(0.3f));
        ui.ring(sxp, syp, 8, 2, kHud.withA(0.9f));
      }
    }

    // ---- damage schematic + ship status
    float sx0 = 30, sy0 = H - 306;
    drawSchematic(p.ship, sx0, sy0);
    float tx = sx0 + 264, ty = sy0 + 4;
    ui.rect(tx - 6, sy0, 262, 276, kPanel);
    auto meter = [&](const char* label, float frac, Color c, const std::string& val) {
      ui.text(tx, ty, 1.8f, kDim, label);
      ui.textR(tx + 244, ty, 1.8f, c, val);
      ui.bar(tx, ty + 16, 244, 7, frac, c, Color{0.08f, 0.12f, 0.16f, 0.9f});
      ty += 32;
    };
    float hull = clampf((st.hullIntegrity - kDeathIntegrity) / (1.0f - kDeathIntegrity), 0.0f, 1.0f);
    meter("SHIELD", p.shield / p.shieldMax, Color{0.35f, 0.65f, 1, 1}, fmt("%.0f", p.shield));
    meter("HULL", hull, healthColor(hull), fmt("%.0f%%", hull * 100));
    meter("ENERGY", p.energy / 100.0f, kAmber, fmt("%.0f", p.energy));
    auto sys = [&](const char* label, float f) {
      Color c = f <= 0.001f ? kRed : healthColor(f);
      ui.text(tx, ty, 1.8f, kDim, label);
      ui.textR(tx + 244, ty, 1.8f, c, f <= 0.001f ? "OFFLINE" : fmt("%.0f%%", f * 100));
      ty += 21;
    };
    ty += 4;
    sys("PORT ENGINE", st.engine[0]);
    sys("STBD ENGINE", st.engine[1]);
    sys("PITCH CTRL", st.authority[0]);
    sys("YAW CTRL", st.authority[1]);
    sys("ROLL CTRL", st.authority[2]);
    sys("THRUSTERS", st.rcs);
    sys("SENSORS", st.sensors);
    ui.text(tx, ty + 2, 1.6f, kDim, fmt("MASS %.1f T   PARTS %d/%d", st.mass / 1000.0f, p.ship.aliveCount(), (int)p.ship.parts.size()));

    // ---- speed / flight mode
    float speed = length(p.vel);
    Vec3 vl = rotate(conj(p.rot), p.vel);
    ui.rect(cx - 150, H - 96, 300, 76, kPanel);
    ui.textC(cx, H - 88, 4.5f, kWhite, fmt("%.0f", speed));
    ui.textC(cx, H - 48, 1.6f, kDim, "M/S");
    ui.text(cx - 140, H - 34, 1.6f, assist ? kGreen : kAmber, assist ? "ASSIST ON" : "ASSIST OFF");
    if (p.boosting) ui.textR(cx + 140, H - 34, 1.6f, kAmber, "BOOST");
    else if (p.in.brake) ui.textR(cx + 140, H - 34, 1.6f, kAmber, "BRAKING");
    ui.bar(cx - 140, H - 18, 280, 4, p.mainOut, kHud, Color{0.08f, 0.12f, 0.16f, 0.9f});
    // drift marker: sideways slip relative to the nose
    if (speed > 3) {
      float dx = clampf(vl.x / 60.0f, -1, 1) * 60, dy = clampf(-vl.y / 60.0f, -1, 1) * 22;
      ui.rect(cx - 62, H - 110, 124, 2, kDim.withA(0.5f));
      ui.rect(cx - 2 + dx, H - 116 + dy * 0.0f, 4, 14, kHud);
      ui.text(cx + 72, H - 116, 1.4f, kDim, "DRIFT");
    }

    // ---- weapons
    for (int k = 0; k < 2; ++k) {
      float x = W - 340, y = H - 180 + k * 82.0f, w = 310, h = 72;
      const WeaponDef& wd = WEAPONS[p.ship.weapon[k]];
      float cond = st.weapon[k];
      ui.rect(x, y, w, h, kPanel);
      ui.frame(x, y, w, h, 2, cond <= 0 ? kRed : kDim);
      ui.rect(x + 2, y + 2, 6, h - 4, Color{wd.color.x, wd.color.y, wd.color.z, cond > 0 ? 1.0f : 0.3f});
      ui.text(x + 18, y + 9, 1.6f, kDim, k == 0 ? "1  LMB" : "2  RMB");
      ui.text(x + 18, y + 27, 2.2f, cond > 0 ? kWhite : kRed, wd.name);
      if (cond <= 0) ui.textR(x + w - 12, y + 9, 1.8f, kRed, "OFFLINE");
      else if (p.misfire[k] > 0) ui.textR(x + w - 12, y + 9, 1.8f, kAmber, "MISFIRE");
      else ui.textR(x + w - 12, y + 9, 1.8f, healthColor(cond), fmt("%.0f%%", cond * 100));
      float rf = cond > 0 ? 1.0f - clampf(p.reload[k] / std::max(p.reloadMax[k], 0.01f), 0.0f, 1.0f) : 0.0f;
      ui.bar(x + 18, y + 54, w - 32, 7, rf, rf >= 1 ? kGreen : kAmber, Color{0.08f, 0.12f, 0.16f, 0.9f});
    }

    // ---- radar (top right): forward is up
    float rx = W - 120, ry = 120, rr = 86;
    ui.disc(rx, ry, rr, Color{0.02f, 0.06f, 0.09f, 0.6f});
    ui.ring(rx, ry, rr, 2, kDim);
    ui.ring(rx, ry, rr * 0.5f, 1, kDim.withA(0.4f));
    ui.line(rx, ry - 8, rx - 6, ry + 6, 2, kHud); ui.line(rx, ry - 8, rx + 6, ry + 6, 2, kHud);
    Quat inv = conj(p.rot);
    for (const ShipEntity& e : world.ships) {
      if (!e.alive || e.team == p.team) continue;
      Vec3 l = rotate(inv, e.pos - p.pos);
      float d = length(l);
      float rd = std::min(1.0f, std::sqrt(d / 2500.0f)) * (rr - 8);
      float hl = std::sqrt(l.x * l.x + l.z * l.z) + 1e-3f;
      float px = rx + l.x / hl * rd, py = ry + l.z / hl * rd;
      Color c = e.id == p.target ? kRed : kAmber;
      ui.disc(px, py, 4, c);
      if (std::fabs(l.y) > 40) ui.line(px, py, px, py - clampf(l.y / d, -1, 1) * 14, 1.5f, c.withA(0.7f)); // above / below stalk
    }

    if (showHelp || playTime < 7) drawHelp(playTime < 7 && !showHelp ? 120.0f : 380.0f);
    if (playTime > 7 && playTime < 14 && !showHelp) ui.textC(cx, 104, 1.6f, kDim.withA(0.8f), "H / F1  CONTROLS");
  }

  // ------------------------------------------------------------------ frame
  void beginFrame(bool hangar) {
    fd.clear();
    cam.aspect = (float)renderer.width() / (float)std::max(1, renderer.height());
    cam.update();
    renderEnvironment(fd, world.sector, cam, uiTime, hangar);
    ui.begin(&fd.ui, renderer.width(), renderer.height());
  }

  void toggleFullscreen() {
    if (windowed) {
      glfwGetWindowPos(win, &winX, &winY);
      glfwGetWindowSize(win, &winW, &winH);
      GLFWmonitor* m = glfwGetPrimaryMonitor();
      const GLFWvidmode* v = glfwGetVideoMode(m);
      glfwSetWindowMonitor(win, m, 0, 0, v->width, v->height, v->refreshRate);
    } else {
      glfwSetWindowMonitor(win, nullptr, winX, winY, winW, winH, 0);
    }
    windowed = !windowed;
    firstMouse = true;
  }

  int run() {
    if (!glfwInit()) { std::fprintf(stderr, "GLFW init failed\n"); return 1; }
    if (!glfwVulkanSupported()) { std::fprintf(stderr, "No Vulkan loader / driver found. Install your GPU's Vulkan driver.\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWmonitor* mon = opt.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    int w = opt.width, h = opt.height;
    if (mon) { const GLFWvidmode* v = glfwGetVideoMode(mon); w = v->width; h = v->height; windowed = false; }
    win = glfwCreateWindow(w, h, "Hullbreaker", mon, nullptr);
    if (!win) { std::fprintf(stderr, "window creation failed\n"); return 1; }
    glfwSetWindowUserPointer(win, this);
    glfwSetKeyCallback(win, [](GLFWwindow* wnd, int k, int, int action, int) {
      Game* g = (Game*)glfwGetWindowUserPointer(wnd);
      if (k < 0 || k > GLFW_KEY_LAST) return;
      if (action == GLFW_PRESS) { g->keys[k] = true; g->pressed[k] = true; }
      else if (action == GLFW_RELEASE) g->keys[k] = false;
    });
    glfwSetMouseButtonCallback(win, [](GLFWwindow* wnd, int b, int action, int) {
      Game* g = (Game*)glfwGetWindowUserPointer(wnd);
      if (b < 0 || b >= 8) return;
      g->mouse[b] = action == GLFW_PRESS;
      if (action == GLFW_PRESS) g->mousePressed[b] = true;
    });
    glfwSetCursorPosCallback(win, [](GLFWwindow* wnd, double x, double y) {
      Game* g = (Game*)glfwGetWindowUserPointer(wnd);
      if (g->firstMouse) { g->lastMx = x; g->lastMy = y; g->firstMouse = false; }
      g->mdx += (float)(x - g->lastMx);
      g->mdy += (float)(y - g->lastMy);
      g->lastMx = g->mx = x;
      g->lastMy = g->my = y;
    });
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow* wnd, int, int) { ((Game*)glfwGetWindowUserPointer(wnd))->renderer.notifyResize(); });
    glfwSetWindowFocusCallback(win, [](GLFWwindow* wnd, int focused) {
      Game* g = (Game*)glfwGetWindowUserPointer(wnd);
      if (!focused && g->state == ST_PLAY && !g->opt.autopilot) { g->state = ST_PAUSE; g->pauseSel = 0; g->setCursor(false); }
    });

    RendererOptions ro;
    ro.vsync = opt.vsync; ro.msaa = opt.msaa; ro.validation = opt.validation;
    if (!renderer.init(win, ro)) return 1;
    std::printf("Hullbreaker: %s, %dx%d, MSAA x%d\n", renderer.gpuName().c_str(), renderer.width(), renderer.height(), renderer.samples());

    loadout[0] = opt.weapon[0]; loadout[1] = opt.weapon[1];
    difficulty = opt.difficulty < 0 ? 0 : (opt.difficulty >= DIFFICULTY_COUNT ? DIFFICULTY_COUNT - 1 : opt.difficulty);
    audio.init(opt.sound);
    std::printf("Audio: %s\n", audio.hasDevice() ? "output device opened" : "no output device (silent)");
    ui.begin(&fd.ui, renderer.width(), renderer.height());
    if (opt.start == "play") startGame();
    else if (opt.start == "hangar") { world.generate(newSeed(), -1, loadout[0], loadout[1], false); enterHangar(); }
    else enterMenu();
    if (opt.warp > 0 && state == ST_PLAY) {
      for (float t = 0; t < opt.warp && world.result == 0; t += 1.0f / 120.0f) world.update(1.0f / 120.0f);
      playTime = opt.warp;
      world.sounds.clear();
      camRot = world.player().rot;
      camPos = world.player().pos + rotate(camRot, Vec3(0, 5.6f, 21));
    }

    double last = glfwGetTime(), logT = 0;
    bool loggedEnd = false;
    world.logMessages = opt.log;
    int frame = 0;
    try {
      while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = opt.fixedDt > 0 ? opt.fixedDt : std::min((float)(now - last), 0.1f);
        last = now;
        uiTime += dt;
        if (hit(GLFW_KEY_F11)) toggleFullscreen();
        if (hit(GLFW_KEY_F5) || hit(GLFW_KEY_F6)) {
          audio.setMusicVolume(audio.musicVolume() + (hit(GLFW_KEY_F6) ? 0.1f : -0.1f));
          mutedVolume = -1;
          showToast(fmt("MUSIC VOLUME %d%%", (int)(audio.musicVolume() * 100 + 0.5f)));
        }
        if (hit(GLFW_KEY_F7) || hit(GLFW_KEY_F8)) {
          audio.setSfxVolume(audio.sfxVolume() + (hit(GLFW_KEY_F8) ? 0.1f : -0.1f));
          showToast(fmt("EFFECTS VOLUME %d%%", (int)(audio.sfxVolume() * 100 + 0.5f)));
          audio.play(SFX_HULL_HIT);
        }
        if (hit(GLFW_KEY_M)) {
          if (mutedVolume < 0) { mutedVolume = audio.musicVolume(); audio.setMusicVolume(0); showToast("MUSIC OFF"); }
          else { audio.setMusicVolume(mutedVolume); mutedVolume = -1; showToast("MUSIC ON"); }
        }

        switch (state) {
          case ST_MENU: updateMenu(dt); break;
          case ST_HANGAR: updateHangar(dt); break;
          case ST_PLAY: updatePlay(dt); break;
          case ST_PAUSE: updatePause(); break;
          case ST_END: updateEnd(dt); break;
        }
        if (toastT > 0) {
          toastT -= dt;
          float tw = UI::textW(toast, 2.2f) + 40;
          ui.rect(ui.W * 0.5f - tw * 0.5f, 126, tw, 34, kPanel);
          ui.textC(ui.W * 0.5f, 135, 2.2f, kAmber.withA(clampf(toastT * 2.0f, 0.0f, 1.0f)), toast);
        }
        renderer.draw(fd);

        if (opt.log && (state == ST_PLAY || state == ST_END)) {
          logT += dt;
          if (logT >= 5.0 || (state == ST_END && !loggedEnd)) {
            loggedEnd = state == ST_END;
            logT = 0;
            const ShipEntity& p = world.player();
            std::printf("t=%6.1f hostiles=%d/%d hull=%.2f shield=%.0f parts=%d/%d eng=%.2f/%.2f wpn=%.2f/%.2f result=%d\n", playTime,
                        world.enemiesAlive, world.enemiesTotal, p.ship.stats.hullIntegrity, p.shield, p.ship.aliveCount(),
                        (int)p.ship.parts.size(), p.ship.stats.engine[0], p.ship.stats.engine[1], p.ship.stats.weapon[0], p.ship.stats.weapon[1], world.result);
            if (state == ST_END) {
              std::printf("  player shots=%d hits=%d\n", p.shotsFired, p.shotsHit);
              for (const ShipEntity& e : world.ships)
                if (!e.player) std::printf("  %-12s alive=%d hull=%.2f parts=%d/%d eng=%.2f/%.2f wpn=%.2f/%.2f %s+%s shots=%d hits=%d\n", e.callsign.c_str(), (int)e.alive,
                    e.ship.stats.hullIntegrity, e.ship.aliveCount(), (int)e.ship.parts.size(), e.ship.stats.engine[0], e.ship.stats.engine[1],
                    e.ship.stats.weapon[0], e.ship.stats.weapon[1], WEAPONS[e.ship.weapon[0]].name, WEAPONS[e.ship.weapon[1]].name, e.shotsFired, e.shotsHit);
            }
            std::fflush(stdout);
          }
        }
        std::memset(pressed, 0, sizeof(pressed));
        std::memset(mousePressed, 0, sizeof(mousePressed));
        mdx = mdy = 0;
        if (opt.frames > 0 && ++frame >= opt.frames) break;
        if (opt.frames < 0 && loggedEnd) break;
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "fatal: %s\n", e.what());
    }
    audio.shutdown();
    renderer.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
  }
};

} // namespace

int runGame(const GameOptions& opt) {
  Game* g = new Game();
  g->opt = opt;
  int r = g->run();
  delete g;
  return r;
}
