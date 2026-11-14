# Hullbreaker

A Linux-native space combat game written in C++17 on Vulkan, in the spirit of Everspace.
You fly one fighter built from about 128 individual parts. Every part has its own
health and can be shot off, and the ship's performance is recomputed from whatever is
still attached.

## Build

Dependencies: a C++17 compiler, CMake, the Vulkan headers/loader, GLFW 3.3+, and the
glslang shader compiler. Audio uses miniaudio, a single header bundled in `third_party/`,
which talks to PulseAudio, PipeWire or ALSA at run time, so there is nothing extra to
install. There are no asset files; meshes, sky, planets, the font, every sound effect and
the music are generated in code.

    # Debian / Ubuntu
    sudo apt install build-essential cmake libvulkan-dev libglfw3-dev glslang-tools
    # Fedora
    sudo dnf install gcc-c++ cmake vulkan-loader-devel glfw-devel glslang
    # Arch
    sudo pacman -S base-devel cmake vulkan-headers vulkan-icd-loader glfw glslang

    cmake -S . -B build
    cmake --build build -j
    ./build/hullbreaker

You also need a Vulkan driver for your GPU (Mesa or the NVIDIA driver).
`./build/hullbreaker --help` lists the command-line options; `--selftest` runs the
headless checks of the damage and flight model.

## Controls

| Input | Action |
| --- | --- |
| Mouse | Steer: the cursor's offset from centre sets pitch / yaw rate |
| W / S | Thrust forward / reverse |
| A / D, Space / Ctrl | Strafe sideways, up / down |
| Q / E | Roll |
| Shift | Boost (drains energy) |
| X | Brake to a stop |
| Z | Flight assist on / off |
| LMB / RMB | Fire weapon slot 1 (port) / slot 2 (starboard) |
| Tab, R | Next target, nearest target |
| F | Centre the steering cursor |
| I | Invert pitch |
| H / F1 | Show controls |
| Esc | Pause |
| F11 | Fullscreen |
| F5 / F6 | Music volume down / up |
| F7 / F8 | Effects volume down / up |
| M | Mute music |

In the menu, Left / Right changes the difficulty; in the hangar, F does, and holding the
right mouse button lets you look around the bay.

## How it works

**Physics.** Ships are rigid bodies with mass, centre of mass and inertia summed from
their surviving parts, integrated with Euler's equations at 120 Hz. With flight assist
off, flight is purely Newtonian: thrust changes velocity and nothing slows you down.
With it on, the flight computer fires thrusters to hold the speed you ask for, but only
with the thrust the ship still has. Projectiles inherit the shooter's velocity, guns
recoil, hits push and spin the target, and collisions exchange momentum.

**Damage.** A hit is traced against each part's bounding box, so the part you hit is the
part that takes damage. Parts form a structural tree: shoot off a wing root and the
whole wing, with its weapon, tumbles away as debris.

- Engines: thrust scales with the health of the nacelle, nozzle, intake and pylon.
  One dead engine halves acceleration and pulls the nose sideways; two leave you adrift.
- Wings, fins and thrusters: each contributes to pitch, yaw or roll authority. Losing
  them makes the ship sluggish, and a lopsided ship rolls by itself in turns.
- Cannons: damage slows the reload, widens the spread and causes misfires. No barrels,
  housing or pylon means the weapon is offline.
- Sensors, shield generator, reactor and canopy each have their own effect.
- The ship is lost when the cockpit, reactor or keel goes, or half its structure is gone.

**Sectors.** Each game generates one of six environments from a seed: asteroid belt,
nebula, planetary orbit, stellar corona, ice field or ship graveyard, with 2 to 6 enemy
ships carrying random loadouts and pilot skill.

**Difficulty.** Four levels (Rookie, Pilot, Veteran, Ace) set how many raiders spawn, how
hard and how accurately they shoot, and how many attack at once while the rest hold back.

**Sound and music.** Every effect is synthesized at start-up. The music is composed while
it plays: each sector's seed picks a key, mode, tempo, chord progressions and patterns,
and the arrangement follows the fight, from a quiet pad and melody when nothing is near
to bass, arpeggio and drums in combat. `--render-audio out.wav 60 --seed N` writes a
piece to a WAV file.

**Shading.** Lit surfaces use a physically based model (GGX specular, Fresnel) with two
lights and an analytic reflection of the surroundings: the star, nebula and planets in
space, or the light bars and bay door in the hangar. The hull is brushed, worn metal with
painted panels.

**Weapons.** Light: pulse laser, Vulcan gatling, ion blaster. Heavy: railgun, plasma
cannon, seeker missile. Pick two in the hangar.

## Source layout

    src/renderer.*      Vulkan renderer (sky, instanced meshes, particles, UI)
    src/meshes.*        procedural meshes
    src/ship.*          the part-based ship and its derived flight stats
    src/world.*         sectors, physics, weapons, damage, AI
    src/render_world.*  simulation state -> draw lists
    src/game.cpp        menus, hangar, camera, HUD
    src/audio.*         sound-effect synthesis, generative music, mixer
    src/selftest.cpp    headless checks
    third_party/        miniaudio.h (public domain / MIT-0)
    shaders/            GLSL, compiled to SPIR-V and embedded at build time
