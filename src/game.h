#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#define MAX_ZOMBIES 32
#define MAX_ALIVE_ZOMBIES 24
#define MAX_SLOTS 2
#define ZONE_COUNT 2

#define START_POINTS 500
#define PLAYER_BASE_HEALTH 100.0f
#define PERK_MAX_HEALTH 250.0f
#define PLAYER_EYE_HEIGHT 1.7f
#define PLAYER_RADIUS 0.4f
#define ZOMBIE_RADIUS 0.4f
#define ZOMBIE_DAMAGE 40.0f
#define ZOMBIE_DYING_TIME 0.7f
#define INTERMISSION_TIME 5.0f
#define REGEN_DELAY 3.0f
#define RESTART_DELAY 1.5f
#define DAMAGE_FLASH_TIME 0.35f
#define ZOMBIE_HIT_FLASH_TIME 0.12f
#define WALL_HEIGHT 3.5f

typedef struct FrameInput {
    Vector2 move; /* x strafes right, y moves forward, length <= 1 */
    Vector2 look; /* radians: x turns right, y looks up */
    bool fire_held;
    bool fire_pressed;
    bool reload_pressed;
    bool interact_pressed;
    bool swap_pressed;
    bool sprint_held;
    bool restart_pressed;
} FrameInput;

typedef enum GamePhase {
    PHASE_INTERMISSION,
    PHASE_ROUND_ACTIVE,
    PHASE_GAME_OVER,
} GamePhase;

typedef enum WeaponId {
    WEAPON_PISTOL,
    WEAPON_SMG,
    WEAPON_SHOTGUN,
    WEAPON_COUNT,
} WeaponId;

typedef struct WeaponDef {
    const char *name;
    float damage; /* per pellet, before the headshot multiplier */
    float fire_interval;
    bool automatic;
    int mag_size;
    int reserve_max;
    float reload_time;
    int pellets;
    float spread; /* half-angle of the pellet square, radians */
    int wall_cost; /* 0 means not for sale */
} WeaponDef;

extern const WeaponDef WEAPONS[WEAPON_COUNT];

typedef struct WeaponSlot {
    WeaponId id;
    int mag;
    int reserve;
} WeaponSlot;

typedef struct Player {
    Vector3 pos; /* feet position, y is always 0 */
    float yaw; /* 0 looks down -Z, positive turns toward +X */
    float pitch;
    float health;
    float max_health;
    float time_since_damage;
    int points;
    WeaponSlot slots[MAX_SLOTS];
    int slot_count;
    int active;
    float fire_cooldown;
    float reload_timer; /* above 0 means a reload is in progress */
    float hitmarker_timer;
    bool hitmarker_kill;
    float muzzle_timer;
    float damage_flash;
} Player;

typedef enum ZombieState {
    ZOMBIE_FREE,
    ZOMBIE_CHASE,
    ZOMBIE_ATTACK,
    ZOMBIE_DYING,
} ZombieState;

typedef struct Zombie {
    ZombieState state;
    Vector3 pos;
    float yaw;
    float health;
    float max_health;
    float speed;
    /* Attack cooldown while chasing, wind-up while attacking. */
    float attack_timer;
    float dying_timer;
    float hit_flash;
    int zone;
} Zombie;

typedef struct Zone {
    float min_x, max_x, min_z, max_z;
    Vector2 portal; /* XZ point of the doorway that leads out of this zone */
} Zone;

typedef struct SpawnPoint {
    Vector3 pos;
    int zone;
} SpawnPoint;

typedef struct Door {
    BoundingBox box;
    int cost;
    int opens_zone;
} Door;

typedef enum InteractKind {
    INTERACT_WALL_BUY,
    INTERACT_DOOR,
    INTERACT_PERK,
} InteractKind;

typedef struct Interactable {
    InteractKind kind;
    BoundingBox box;
    bool solid;
    float radius;
    int payload; /* weapon id, door index, or perk cost */
} Interactable;

typedef struct InteractionInfo {
    bool active;
    int cost;
    const char *verb;
    const char *subject;
} InteractionInfo;

extern const BoundingBox LEVEL_WALLS[];
extern const int LEVEL_WALL_COUNT;
extern const Zone LEVEL_ZONES[ZONE_COUNT];
extern const Door LEVEL_DOORS[];
extern const int LEVEL_DOOR_COUNT;
extern const SpawnPoint LEVEL_SPAWNS[];
extern const int LEVEL_SPAWN_COUNT;
extern const Interactable LEVEL_INTERACTABLES[];
extern const int LEVEL_INTERACTABLE_COUNT;

typedef struct GameState {
    Player player;
    Zombie zombies[MAX_ZOMBIES];
    GamePhase phase;
    float phase_timer; /* seconds spent in the current phase */
    int round_number;
    int zombies_left_to_spawn;
    float spawn_timer;
    bool zone_open[ZONE_COUNT];
    int player_zone;
    int kills;
    int rounds_survived;
    uint32_t rng_state;
} GameState;

void GameInit(GameState *g, uint32_t seed);
void GameUpdate(GameState *g, const FrameInput *in, float dt);

bool GameBlocked(const GameState *g, Vector3 pos, float radius);
bool GameInteractableVisible(const GameState *g, int index, Vector3 eye);
int GameAliveZombies(const GameState *g);
int GameNearestInteractable(const GameState *g);
InteractionInfo GameDescribeInteractable(const GameState *g, int index);
bool PlayerHasPerk(const Player *p);
Vector3 PlayerEye(const Player *p);
Vector3 PlayerForward(const Player *p);
BoundingBox ZombieBodyBox(const Zombie *z);
BoundingBox ZombieHeadBox(const Zombie *z);
Vector3 BoxCenter(BoundingBox box);

#endif
