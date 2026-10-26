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
#define HEADSHOT_MULTIPLIER 2.0f
#define PLAYER_BASE_HEALTH 100.0f
#define PERK_MAX_HEALTH 250.0f
#define QUICK_HANDS_RELOAD_SCALE 0.5f
#define PLAYER_EYE_HEIGHT 1.7f
#define CROUCH_EYE_HEIGHT 1.0f
#define PLAYER_HEAD_CLEARANCE 0.1f
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
#define GAME_EVENT_CAPACITY 64

typedef struct FrameInput {
    Vector2 move; /* x strafes right, y moves forward, length <= 1 */
    Vector2 look; /* radians: x turns right, y looks up */
    bool fire_held;
    bool fire_pressed;
    bool reload_pressed;
    bool interact_pressed;
    bool swap_pressed;
    bool sprint_held;
    bool crouch_held;
    bool jump_pressed;
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

typedef enum PerkId {
    PERK_VITALITY,
    PERK_QUICK_HANDS,
    PERK_COUNT,
} PerkId;

typedef struct PerkDef {
    const char *name;
    int cost;
} PerkDef;

extern const PerkDef PERKS[PERK_COUNT];

typedef struct Player {
    Vector3 pos; /* feet position, y is the height above the floor */
    float vertical_speed;
    float eye_height; /* eases between PLAYER_EYE_HEIGHT and CROUCH_EYE_HEIGHT */
    float yaw; /* 0 looks down -Z, positive turns toward +X */
    float pitch;
    float health;
    float max_health;
    uint8_t perks; /* bit PerkId is set once that perk is bought */
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
    float growl_timer;
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
    int payload; /* weapon id, door index, or PerkId */
} Interactable;

typedef struct InteractionInfo {
    bool active;
    int cost;
    const char *verb;
    const char *subject;
} InteractionInfo;

extern const BoundingBox LEVEL_WALLS[];
extern const int LEVEL_WALL_COUNT;
extern const BoundingBox LEVEL_OVERHANGS[];
extern const int LEVEL_OVERHANG_COUNT;
extern const Zone LEVEL_ZONES[ZONE_COUNT];
extern const Door LEVEL_DOORS[];
extern const int LEVEL_DOOR_COUNT;
extern const SpawnPoint LEVEL_SPAWNS[];
extern const int LEVEL_SPAWN_COUNT;
extern const Interactable LEVEL_INTERACTABLES[];
extern const int LEVEL_INTERACTABLE_COUNT;

typedef enum GameEventKind {
    EVENT_FIRE, /* arg is the WeaponId */
    EVENT_RELOAD,
    EVENT_HIT,
    EVENT_KILL,
    EVENT_HEADSHOT_KILL,
    EVENT_ZOMBIE_GROWL,
    EVENT_ZOMBIE_ATTACK,
    EVENT_PLAYER_DAMAGE,
    EVENT_ROUND_START,
    EVENT_ROUND_END,
    EVENT_PURCHASE,
    EVENT_PURCHASE_DENIED,
} GameEventKind;

#define MAX_DAMAGE_NUMBERS 32
#define DAMAGE_NUMBER_TIME 0.8f

typedef struct DamageNumber {
    Vector3 pos;     /* world point where the first pellet hit */
    float amount;    /* damage applied, headshot multiplier included */
    float timer;     /* counts down from DAMAGE_NUMBER_TIME, 0 means the slot is free */
    int zombie;      /* index into GameState.zombies */
    bool headshot;
    bool kill;
} DamageNumber;

typedef struct GameEvent {
    GameEventKind kind;
    int arg;
} GameEvent;

typedef struct GameState {
    Player player;
    Zombie zombies[MAX_ZOMBIES];
    DamageNumber damage_numbers[MAX_DAMAGE_NUMBERS];
    GamePhase phase;
    float phase_timer; /* seconds spent in the current phase */
    int round_number;
    int zombies_left_to_spawn;
    float spawn_timer;
    bool zone_open[ZONE_COUNT];
    int player_zone;
    int kills;
    int rounds_survived;
    bool god_mode;
    uint32_t rng_state;
    /* Filled by GameUpdate, which empties it first. The caller drains it after each step. Events past capacity are dropped. */
    GameEvent events[GAME_EVENT_CAPACITY];
    int event_count;
} GameState;

void GameInit(GameState *g, uint32_t seed);
void GameUpdate(GameState *g, const FrameInput *in, float dt);

bool GameBlocked(const GameState *g, Vector3 pos, float radius);
bool GameInteractableVisible(const GameState *g, int index, Vector3 eye);
int GameAliveZombies(const GameState *g);
int GameNearestInteractable(const GameState *g);
InteractionInfo GameDescribeInteractable(const GameState *g, int index);
typedef enum DebugKind {
    DEBUG_POINTS,
    DEBUG_WEAPON,
    DEBUG_PERK,
    DEBUG_REFILL_AMMO,
    DEBUG_HEAL,
    DEBUG_GOD_MODE,
    DEBUG_OPEN_DOORS,
    DEBUG_SKIP_ROUND,
} DebugKind;

typedef struct DebugCommand {
    const char *label;
    DebugKind kind;
    int arg; /* points, WeaponId, or PerkId, depending on kind */
} DebugCommand;

extern const DebugCommand DEBUG_COMMANDS[];
extern const int DEBUG_COMMAND_COUNT;

void GameDebugApply(GameState *g, const DebugCommand *cmd);
bool PlayerHasPerk(const Player *p, PerkId id);
float ReloadDuration(const Player *p, WeaponId id);
Vector3 PlayerEye(const Player *p);
Vector3 PlayerForward(const Player *p);
BoundingBox ZombieBodyBox(const Zombie *z);
BoundingBox ZombieHeadBox(const Zombie *z);
Vector3 BoxCenter(BoundingBox box);

#endif
