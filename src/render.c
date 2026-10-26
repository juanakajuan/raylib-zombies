#include "render.h"

#include <math.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#define TILE_SIZE 2.0f
#define LABEL_RANGE 12.0f
#define VIGNETTE_SIZE 200
#define HEALTH_BAR_HEIGHT 2.05f
#define HEALTH_BAR_WIDTH 0.6f
#define HEALTH_BAR_THICKNESS 0.07f
#define HEALTH_BAR_FILL_LIFT 0.004f
#define DAMAGE_NUMBER_RISE 0.5f

static const Color SKY = {14, 14, 20, 255};
static const Color CEILING = {44, 44, 52, 255};
static const Color WALL = {112, 108, 118, 255};
static const Color OVERHANG = {104, 62, 44, 255};
static const Color WALL_WIRE = {40, 38, 46, 255};
static const Color FLOOR_FRONT[2] = {{94, 86, 78, 255}, {78, 71, 64, 255}};
static const Color FLOOR_BACK[2] = {{68, 78, 92, 255}, {56, 64, 78, 255}};
static const Color DOOR_COLOR = {150, 62, 44, 255};
static const Color PLANK_COLOR = {112, 82, 52, 255};
static const struct {
    Color body;
    Color glow;
} PERK_LOOK[PERK_COUNT] = {
    [PERK_VITALITY] = {{46, 118, 214, 255}, {255, 232, 150, 255}},
    [PERK_QUICK_HANDS] = {{222, 108, 36, 255}, {150, 255, 160, 255}},
};
static const Color SKIN = {104, 132, 98, 255};
static const Color SHIRT = {82, 66, 58, 255};
static const Color PANTS = {52, 58, 76, 255};
static const Color EYES = {255, 50, 40, 255};
static const Color GUN_METAL = {38, 40, 46, 255};
static const Color GUN_GRIP = {86, 58, 38, 255};
static const Color MUZZLE_FLASH = {255, 222, 130, 255};
static const Color HEALTH_BAR_BACK = {12, 10, 10, 255};
static const Color HEALTH_BAR_FILL = {200, 30, 30, 255};
static const Color DAMAGE_BODY = {255, 255, 255, 255};
static const Color DAMAGE_HEADSHOT = {255, 220, 60, 255};
static const Color DAMAGE_KILL = {255, 120, 70, 255};

static const Color WEAPON_PANEL[WEAPON_COUNT] = {
    [WEAPON_PISTOL] = {150, 150, 150, 255},
    [WEAPON_SMG] = {226, 140, 40, 255},
    [WEAPON_SHOTGUN] = {196, 52, 52, 255},
};
static const float WEAPON_VIEW_LENGTH[WEAPON_COUNT] = {
    [WEAPON_PISTOL] = 0.12f,
    [WEAPON_SMG] = 0.18f,
    [WEAPON_SHOTGUN] = 0.22f,
};

static Color Shade(Color color, float factor)
{
    return (Color){(unsigned char)((float)color.r * factor), (unsigned char)((float)color.g * factor),
                   (unsigned char)((float)color.b * factor), color.a};
}

static void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
{
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
    rlVertex3f(d.x, d.y, d.z);
}

/* raylib's DrawCube is unlit, so each face gets its own brightness to keep the shapes readable. */
static void DrawBox(Vector3 center, Vector3 size, Color color)
{
    float x0 = center.x - size.x * 0.5f, x1 = center.x + size.x * 0.5f;
    float y0 = center.y - size.y * 0.5f, y1 = center.y + size.y * 0.5f;
    float z0 = center.z - size.z * 0.5f, z1 = center.z + size.z * 0.5f;
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    Quad((Vector3){x0, y1, z1}, (Vector3){x1, y1, z1}, (Vector3){x1, y1, z0}, (Vector3){x0, y1, z0}, color);
    Quad((Vector3){x0, y0, z1}, (Vector3){x1, y0, z1}, (Vector3){x1, y1, z1}, (Vector3){x0, y1, z1}, Shade(color, 0.8f));
    Quad((Vector3){x1, y0, z0}, (Vector3){x0, y0, z0}, (Vector3){x0, y1, z0}, (Vector3){x1, y1, z0}, Shade(color, 0.7f));
    Quad((Vector3){x1, y0, z1}, (Vector3){x1, y0, z0}, (Vector3){x1, y1, z0}, (Vector3){x1, y1, z1}, Shade(color, 0.62f));
    Quad((Vector3){x0, y0, z0}, (Vector3){x0, y0, z1}, (Vector3){x0, y1, z1}, (Vector3){x0, y1, z0}, Shade(color, 0.55f));
    Quad((Vector3){x0, y0, z0}, (Vector3){x1, y0, z0}, (Vector3){x1, y0, z1}, (Vector3){x0, y0, z1}, Shade(color, 0.4f));
    rlEnd();
}

static void DrawSolidBox(BoundingBox box, Color color)
{
    DrawBox(BoxCenter(box), Vector3Subtract(box.max, box.min), color);
}

static void DrawFloorAndCeiling(float min_x, float max_x, float min_z, float max_z, const Color tiles[2])
{
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    int index = 0;
    for (float x = min_x; x < max_x - 0.01f; x += TILE_SIZE, index++) {
        int row = index;
        for (float z = min_z; z < max_z - 0.01f; z += TILE_SIZE, row++) {
            float x1 = fminf(x + TILE_SIZE, max_x);
            float z1 = fminf(z + TILE_SIZE, max_z);
            Quad((Vector3){x, 0.0f, z1}, (Vector3){x1, 0.0f, z1}, (Vector3){x1, 0.0f, z}, (Vector3){x, 0.0f, z}, tiles[row & 1]);
            Quad((Vector3){x, WALL_HEIGHT, z}, (Vector3){x1, WALL_HEIGHT, z}, (Vector3){x1, WALL_HEIGHT, z1}, (Vector3){x, WALL_HEIGHT, z1}, CEILING);
        }
    }
    rlEnd();
}

static void DrawInteractable(const GameState *g, const Interactable *it)
{
    Vector3 center = BoxCenter(it->box);
    Vector3 size = Vector3Subtract(it->box.max, it->box.min);
    switch (it->kind) {
    case INTERACT_WALL_BUY:
        DrawBox(center, Vector3Add(size, (Vector3){-0.03f, 0.14f, 0.14f}), Shade(WALL_WIRE, 1.2f));
        DrawSolidBox(it->box, WEAPON_PANEL[it->payload]);
        break;
    case INTERACT_DOOR:
        if (g->zone_open[LEVEL_DOORS[it->payload].opens_zone]) break;
        DrawSolidBox(it->box, DOOR_COLOR);
        DrawCubeWiresV(center, size, WALL_WIRE);
        DrawBox((Vector3){center.x, 0.9f, center.z}, (Vector3){size.x + 0.1f, 0.2f, size.z + 0.16f}, PLANK_COLOR);
        DrawBox((Vector3){center.x, 2.3f, center.z}, (Vector3){size.x + 0.1f, 0.2f, size.z + 0.16f}, PLANK_COLOR);
        break;
    case INTERACT_PERK: {
        Color glow = PERK_LOOK[it->payload].glow;
        DrawSolidBox(it->box, PERK_LOOK[it->payload].body);
        DrawCubeWiresV(center, size, WALL_WIRE);
        DrawBox((Vector3){center.x, it->box.max.y + 0.08f, center.z}, (Vector3){size.x + 0.1f, 0.16f, size.z + 0.1f}, glow);
        DrawBox((Vector3){center.x, 1.4f, it->box.min.z - 0.02f}, (Vector3){size.x * 0.7f, 0.9f, 0.06f}, glow);
        break;
    }
    }
}

static void DrawLevel(const GameState *g)
{
    DrawFloorAndCeiling(LEVEL_ZONES[0].min_x, LEVEL_ZONES[0].max_x, LEVEL_ZONES[0].min_z, LEVEL_ZONES[0].max_z, FLOOR_FRONT);
    DrawFloorAndCeiling(LEVEL_ZONES[1].min_x, LEVEL_ZONES[1].max_x, LEVEL_ZONES[1].min_z, LEVEL_ZONES[1].max_z, FLOOR_BACK);
    for (int i = 0; i < LEVEL_WALL_COUNT; i++) {
        DrawSolidBox(LEVEL_WALLS[i], WALL);
        DrawCubeWiresV(BoxCenter(LEVEL_WALLS[i]), Vector3Subtract(LEVEL_WALLS[i].max, LEVEL_WALLS[i].min), WALL_WIRE);
    }
    for (int i = 0; i < LEVEL_OVERHANG_COUNT; i++) {
        DrawSolidBox(LEVEL_OVERHANGS[i], OVERHANG);
        DrawCubeWiresV(BoxCenter(LEVEL_OVERHANGS[i]), Vector3Subtract(LEVEL_OVERHANGS[i].max, LEVEL_OVERHANGS[i].min), WALL_WIRE);
    }
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) DrawInteractable(g, &LEVEL_INTERACTABLES[i]);
}

static void DrawZombie(const Zombie *z)
{
    if (z->state == ZOMBIE_FREE) return;
    float flash = z->hit_flash / ZOMBIE_HIT_FLASH_TIME;
    float fall = z->state == ZOMBIE_DYING ? 1.0f - z->dying_timer / ZOMBIE_DYING_TIME : 0.0f;
    float darken = 1.0f - 0.55f * fall;
    Color skin = Shade(ColorLerp(SKIN, RED, flash), darken);
    Color shirt = Shade(ColorLerp(SHIRT, RED, flash), darken);
    Color pants = Shade(ColorLerp(PANTS, RED, flash), darken);

    rlPushMatrix();
    rlTranslatef(z->pos.x, 0.0f, z->pos.z);
    rlRotatef(-z->yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(90.0f * fall * fall, 1.0f, 0.0f, 0.0f);
    DrawBox((Vector3){-0.16f, 0.325f, 0.0f}, (Vector3){0.28f, 0.65f, 0.3f}, pants);
    DrawBox((Vector3){0.16f, 0.325f, 0.0f}, (Vector3){0.28f, 0.65f, 0.3f}, pants);
    DrawBox((Vector3){0.0f, 1.05f, 0.0f}, (Vector3){0.64f, 0.8f, 0.4f}, shirt);
    DrawBox((Vector3){-0.42f, 1.25f, -0.32f}, (Vector3){0.16f, 0.16f, 0.72f}, skin);
    DrawBox((Vector3){0.42f, 1.25f, -0.32f}, (Vector3){0.16f, 0.16f, 0.72f}, skin);
    DrawBox((Vector3){0.0f, 1.65f, 0.0f}, (Vector3){0.4f, 0.4f, 0.4f}, skin);
    if (fall < 0.5f) {
        DrawBox((Vector3){-0.1f, 1.7f, -0.21f}, (Vector3){0.08f, 0.06f, 0.04f}, EYES);
        DrawBox((Vector3){0.1f, 1.7f, -0.21f}, (Vector3){0.08f, 0.06f, 0.04f}, EYES);
    }
    rlPopMatrix();
}

/* The gun stays inside the player's collision radius so it can never poke through a wall. */
static void DrawViewModel(const Player *p, Vector3 eye)
{
    const WeaponSlot *slot = &p->slots[p->active];
    float length = WEAPON_VIEW_LENGTH[slot->id];
    float dip = 0.0f;
    if (p->reload_timer > 0.0f) {
        float progress = 1.0f - p->reload_timer / ReloadDuration(p, slot->id);
        dip = sinf(progress * PI) * 0.1f;
    }
    float kick = p->muzzle_timer > 0.0f ? 0.025f : 0.0f;

    rlPushMatrix();
    rlTranslatef(eye.x, eye.y, eye.z);
    rlRotatef(-p->yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(p->pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
    DrawBox((Vector3){0.1f, -0.085f - dip, -0.12f - length * 0.5f + kick}, (Vector3){0.028f, 0.034f, length}, GUN_METAL);
    DrawBox((Vector3){0.1f, -0.115f - dip, -0.1f + kick}, (Vector3){0.022f, 0.05f, 0.03f}, GUN_GRIP);
    if (p->muzzle_timer > 0.0f) {
        Vector3 tip = {0.1f, -0.085f - dip, -0.13f - length + kick};
        DrawBox(tip, (Vector3){0.05f, 0.012f, 0.05f}, MUZZLE_FLASH);
        DrawBox(tip, (Vector3){0.012f, 0.05f, 0.05f}, MUZZLE_FLASH);
    }
    rlPopMatrix();
}

static void DrawTextShadow(const char *text, int x, int y, int size, Color color)
{
    DrawText(text, x + 2, y + 2, size, (Color){0, 0, 0, 170});
    DrawText(text, x, y, size, color);
}

/* The HUD is laid out for a window UI_REFERENCE_HEIGHT pixels tall. It grows with the window at half the rate of
 * the height, so a 4K screen gets 2x and not 3x. */
#define UI_REFERENCE_HEIGHT 720.0f
#define UI_SCALE_RATE 0.5f

static float UiScale(void)
{
    return 1.0f + ((float)GetScreenHeight() / UI_REFERENCE_HEIGHT - 1.0f) * UI_SCALE_RATE;
}

static int UiHeight(void)
{
    return (int)((float)GetScreenHeight() / UiScale());
}

static int UiWidth(void)
{
    return (int)((float)GetScreenWidth() / UiScale());
}

Vector2 UiMousePosition(void)
{
    Vector2 mouse = GetMousePosition();
    float scale = UiScale();
    return (Vector2){mouse.x / scale, mouse.y / scale};
}

static void DrawTextCentered(const char *text, int center_x, int y, int size, Color color)
{
    DrawTextShadow(text, center_x - MeasureText(text, size) / 2, y, size, color);
}

static void DrawVignette(const Player *p, int width, int height)
{
    float missing = 1.0f - p->health / p->max_health;
    float strength = Clamp(missing * 0.7f + p->damage_flash / DAMAGE_FLASH_TIME * 0.4f, 0.0f, 0.85f);
    if (strength <= 0.01f) return;
    Color edge = {170, 0, 0, (unsigned char)(strength * 255.0f)};
    Color clear = {170, 0, 0, 0};
    DrawRectangleGradientV(0, 0, width, VIGNETTE_SIZE, edge, clear);
    DrawRectangleGradientV(0, height - VIGNETTE_SIZE, width, VIGNETTE_SIZE, clear, edge);
    DrawRectangleGradientH(0, 0, VIGNETTE_SIZE, height, edge, clear);
    DrawRectangleGradientH(width - VIGNETTE_SIZE, 0, VIGNETTE_SIZE, height, clear, edge);
}

static void DrawCrosshair(const Player *p, int width, int height)
{
    int cx = width / 2, cy = height / 2;
    Color color = {255, 255, 255, 220};
    DrawRectangle(cx - 12, cy - 1, 8, 2, color);
    DrawRectangle(cx + 4, cy - 1, 8, 2, color);
    DrawRectangle(cx - 1, cy - 12, 2, 8, color);
    DrawRectangle(cx - 1, cy + 4, 2, 8, color);
    if (p->hitmarker_timer <= 0.0f) return;
    Color mark = p->hitmarker_kill ? (Color){255, 60, 50, 255} : (Color){255, 255, 255, 255};
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            DrawLineEx((Vector2){(float)(cx + sx * 8), (float)(cy + sy * 8)}, (Vector2){(float)(cx + sx * 16), (float)(cy + sy * 16)}, 3.0f, mark);
        }
    }
}

static void DrawDamageNumbers(const GameState *g, Camera3D camera)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    float scale = UiScale();
    for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) {
        const DamageNumber *n = &g->damage_numbers[i];
        if (n->timer <= 0.0f) continue;
        float age = 1.0f - n->timer / DAMAGE_NUMBER_TIME;
        Vector3 anchor = n->pos;
        anchor.y += DAMAGE_NUMBER_RISE * age;
        if (Vector3DotProduct(Vector3Subtract(anchor, camera.position), forward) <= 0.1f) continue;
        Vector2 screen = GetWorldToScreen(anchor, camera);
        Color color = n->kill ? DAMAGE_KILL : n->headshot ? DAMAGE_HEADSHOT : DAMAGE_BODY;
        color.a = (unsigned char)(255.0f * Clamp((1.0f - age) * 2.0f, 0.0f, 1.0f));
        /* Consecutive hits land in different slots, so a per-slot nudge keeps rapid hits on one spot readable. */
        int nudge = (i * 7 % 5 - 2) * 12;
        DrawTextCentered(TextFormat("%d", (int)ceilf(n->amount)), (int)(screen.x / scale) + nudge, (int)(screen.y / scale), n->headshot ? 26 : 22, color);
    }
}

static void DrawInteractableLabels(const GameState *g, Camera3D camera)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    for (int i = 0; i < LEVEL_INTERACTABLE_COUNT; i++) {
        InteractionInfo info = GameDescribeInteractable(g, i);
        if (!info.active) continue;
        Vector3 anchor = BoxCenter(LEVEL_INTERACTABLES[i].box);
        anchor.y = LEVEL_INTERACTABLES[i].box.max.y + 0.4f;
        Vector3 offset = Vector3Subtract(anchor, camera.position);
        if (Vector3Length(offset) > LABEL_RANGE || Vector3DotProduct(offset, forward) <= 0.1f) continue;
        if (!GameInteractableVisible(g, i, camera.position)) continue;
        Vector2 screen = GetWorldToScreen(anchor, camera);
        float scale = UiScale();
        DrawTextCentered(TextFormat("%s [%d]", info.subject, info.cost), (int)(screen.x / scale), (int)(screen.y / scale), 20, (Color){255, 230, 150, 255});
    }
}

static void DrawHud(const GameState *g, Camera3D camera)
{
    const Player *p = &g->player;
    int width = UiWidth(), height = UiHeight();
    const Color blood = {200, 24, 24, 255};

    bool playing = g->phase != PHASE_GAME_OVER;

    rlPushMatrix();
    rlScalef(UiScale(), UiScale(), 1.0f);

    DrawVignette(p, width, height);
    if (playing) {
        DrawInteractableLabels(g, camera);
        DrawDamageNumbers(g, camera);
        DrawCrosshair(p, width, height);
    }

    if (g->god_mode) DrawTextShadow("GOD MODE", width - 170, 20, 24, (Color){255, 230, 120, 255});
    DrawTextShadow(TextFormat("%d", g->round_number), 28, height - 130, 110, blood);
    DrawTextShadow(TextFormat("%d", p->points), 190, height - 78, 44, (Color){255, 230, 120, 255});
    DrawTextShadow("POINTS", 192, height - 100, 18, (Color){255, 255, 255, 190});
    if (g->phase == PHASE_ROUND_ACTIVE) {
        DrawTextShadow(TextFormat("Zombies left: %d", g->zombies_left_to_spawn + GameAliveZombies(g)), 192, height - 36, 20, (Color){255, 255, 255, 200});
    }

    int bar_width = 280;
    int bar_x = width / 2 - bar_width / 2, bar_y = height - 46;
    float ratio = Clamp(p->health / p->max_health, 0.0f, 1.0f);
    DrawRectangle(bar_x - 3, bar_y - 3, bar_width + 6, 26, (Color){0, 0, 0, 160});
    DrawRectangle(bar_x, bar_y, (int)((float)bar_width * ratio), 20, ColorLerp((Color){210, 30, 30, 255}, (Color){70, 200, 90, 255}, ratio));
    DrawTextCentered(TextFormat("%d", (int)ceilf(p->health)), width / 2, bar_y + 1, 18, WHITE);

    const WeaponSlot *slot = &p->slots[p->active];
    DrawTextShadow(WEAPONS[slot->id].name, width - 300, height - 120, 28, WHITE);
    DrawTextShadow(TextFormat("%d / %d", slot->mag, slot->reserve), width - 300, height - 86, 56,
                   slot->mag == 0 ? blood : (Color){255, 255, 255, 255});
    if (p->slot_count > 1) {
        int other = 1 - p->active;
        DrawTextShadow(TextFormat("[%d] %s", other + 1, WEAPONS[p->slots[other].id].name), width - 300, height - 146, 18, (Color){255, 255, 255, 150});
    }
    if (p->reload_timer > 0.0f) DrawTextShadow("RELOADING", width - 300, height - 180, 24, (Color){255, 220, 90, 255});

    int nearby = GameNearestInteractable(g);
    if (playing && nearby >= 0) {
        InteractionInfo info = GameDescribeInteractable(g, nearby);
        bool affordable = p->points >= info.cost;
        const char *prompt = TextFormat("Press E to %s %s [%d]", info.verb, info.subject, info.cost);
        DrawTextCentered(prompt, width / 2, height * 6 / 10, 28, affordable ? WHITE : (Color){255, 110, 90, 255});
    }

    if (g->phase == PHASE_INTERMISSION) {
        DrawTextCentered(TextFormat("ROUND %d", g->round_number), width / 2, height / 7, 96, blood);
        int seconds = (int)ceilf(INTERMISSION_TIME - g->phase_timer);
        DrawTextCentered(TextFormat("Get ready... %d", seconds), width / 2, height / 7 + 104, 28, WHITE);
    }

    if (g->phase == PHASE_GAME_OVER) {
        DrawRectangle(0, 0, width, height, (Color){0, 0, 0, 150});
        DrawTextCentered("GAME OVER", width / 2, height / 4, 110, blood);
        DrawTextCentered(TextFormat("You survived %d rounds", g->rounds_survived), width / 2, height / 4 + 130, 40, WHITE);
        DrawTextCentered(TextFormat("Kills: %d", g->kills), width / 2, height / 4 + 184, 28, (Color){255, 255, 255, 200});
        if (g->phase_timer >= RESTART_DELAY) DrawTextCentered("Press R to restart", width / 2, height / 4 + 250, 32, (Color){255, 230, 120, 255});
    }
    rlPopMatrix();
}

static void DrawHealthBar(const Zombie *z, Vector3 camera_position)
{
    if (z->state != ZOMBIE_CHASE && z->state != ZOMBIE_ATTACK) return;
    Vector3 center = {z->pos.x, HEALTH_BAR_HEIGHT, z->pos.z};
    Vector3 toward = Vector3Normalize(Vector3Subtract(camera_position, center));
    Vector3 right = Vector3Normalize(Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, toward));
    Vector3 up = Vector3CrossProduct(toward, right);
    Vector3 half_width = Vector3Scale(right, HEALTH_BAR_WIDTH * 0.5f);
    Vector3 half_height = Vector3Scale(up, HEALTH_BAR_THICKNESS * 0.5f);
    float ratio = Clamp(z->health / z->max_health, 0.0f, 1.0f);

    rlBegin(RL_QUADS);
    Quad(Vector3Subtract(Vector3Subtract(center, half_width), half_height), Vector3Subtract(Vector3Add(center, half_width), half_height),
         Vector3Add(Vector3Add(center, half_width), half_height), Vector3Add(Vector3Subtract(center, half_width), half_height), HEALTH_BAR_BACK);
    Vector3 fill_center = Vector3Add(center, Vector3Scale(toward, HEALTH_BAR_FILL_LIFT));
    Vector3 left = Vector3Subtract(fill_center, half_width);
    Vector3 edge = Vector3Add(left, Vector3Scale(right, HEALTH_BAR_WIDTH * ratio));
    Quad(Vector3Subtract(left, half_height), Vector3Subtract(edge, half_height), Vector3Add(edge, half_height), Vector3Add(left, half_height),
         HEALTH_BAR_FILL);
    rlEnd();
}

void GameDraw(const GameState *g, float fov_degrees)
{
    const Player *p = &g->player;
    Vector3 eye = PlayerEye(p);
    if (g->phase == PHASE_GAME_OVER) eye.y = Lerp(eye.y, 0.45f, Clamp(g->phase_timer * 2.0f, 0.0f, 1.0f));
    Camera3D camera = {
        .position = eye,
        .target = Vector3Add(eye, PlayerForward(p)),
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = fov_degrees,
        .projection = CAMERA_PERSPECTIVE,
    };

    ClearBackground(SKY);
    BeginMode3D(camera);
    rlDisableBackfaceCulling();
    DrawLevel(g);
    for (int i = 0; i < MAX_ZOMBIES; i++) DrawZombie(&g->zombies[i]);
    for (int i = 0; i < MAX_ZOMBIES; i++) DrawHealthBar(&g->zombies[i], eye);
    if (g->phase != PHASE_GAME_OVER) DrawViewModel(p, eye);
    rlEnableBackfaceCulling();
    EndMode3D();
    DrawHud(g, camera);
}

#define BLUR_DOWNSCALE 2
#define BLUR_SPREAD 2.0f

static const char BLUR_SHADER[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec2 direction;\n"
    "out vec4 finalColor;\n"
    "const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
    "void main()\n"
    "{\n"
    "    vec3 sum = texture(texture0, fragTexCoord).rgb * weights[0];\n"
    "    for (int i = 1; i < 5; i++) {\n"
    "        vec2 offset = direction * float(i);\n"
    "        sum += texture(texture0, fragTexCoord + offset).rgb * weights[i];\n"
    "        sum += texture(texture0, fragTexCoord - offset).rgb * weights[i];\n"
    "    }\n"
    "    finalColor = vec4(sum, 1.0) * colDiffuse * fragColor;\n"
    "}\n";

typedef struct BlurTargets {
    Shader shader;
    int direction_loc;
    RenderTexture2D scene;
    RenderTexture2D half; /* holds the horizontal pass at reduced size */
} BlurTargets;

static RenderTexture2D LoadBlurTexture(int width, int height)
{
    RenderTexture2D target = LoadRenderTexture(width, height);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
    return target;
}

/* Created on first use and again when the window size changes, so a fullscreen toggle in the pause menu stays sharp. */
static BlurTargets *PrepareBlur(int width, int height)
{
    static BlurTargets blur;
    if (blur.shader.id == 0) {
        blur.shader = LoadShaderFromMemory(NULL, BLUR_SHADER);
        blur.direction_loc = GetShaderLocation(blur.shader, "direction");
    }
    if (blur.scene.texture.width != width || blur.scene.texture.height != height) {
        if (blur.scene.id != 0) {
            UnloadRenderTexture(blur.scene);
            UnloadRenderTexture(blur.half);
        }
        blur.scene = LoadBlurTexture(width, height);
        blur.half = LoadBlurTexture(width / BLUR_DOWNSCALE, height / BLUR_DOWNSCALE);
    }
    return &blur;
}

static void DrawBlurPass(const BlurTargets *blur, Texture2D source, Vector2 direction, Rectangle dest)
{
    SetShaderValue(blur->shader, blur->direction_loc, &direction, SHADER_UNIFORM_VEC2);
    BeginShaderMode(blur->shader);
    /* Render textures are stored upside down, so the source rectangle flips them. */
    DrawTexturePro(source, (Rectangle){0.0f, 0.0f, (float)source.width, -(float)source.height}, dest, Vector2Zero(), 0.0f, WHITE);
    EndShaderMode();
}

void GameDrawBlurred(const GameState *g, float fov_degrees)
{
    int width = GetScreenWidth(), height = GetScreenHeight();
    BlurTargets *blur = PrepareBlur(width, height);
    Texture2D scene = blur->scene.texture, half = blur->half.texture;

    BeginTextureMode(blur->scene);
    GameDraw(g, fov_degrees);
    EndTextureMode();

    BeginTextureMode(blur->half);
    DrawBlurPass(blur, scene, (Vector2){BLUR_SPREAD / (float)scene.width, 0.0f}, (Rectangle){0.0f, 0.0f, (float)half.width, (float)half.height});
    EndTextureMode();

    DrawBlurPass(blur, half, (Vector2){0.0f, BLUR_SPREAD / (float)half.height}, (Rectangle){0.0f, 0.0f, (float)width, (float)height});
}

#define PAUSE_ROW_HEIGHT 44
#define PAUSE_BAR_WIDTH 200

Rectangle PauseRowBounds(int row)
{
    int width = UiWidth(), height = UiHeight();
    return (Rectangle){(float)(width / 2 - 260), (float)(height / 3 + 130 + row * PAUSE_ROW_HEIGHT), 620.0f, (float)PAUSE_ROW_HEIGHT};
}

Rectangle PauseBarBounds(int row)
{
    Rectangle r = PauseRowBounds(row);
    return (Rectangle){(float)(UiWidth() / 2 - 20), r.y + 6.0f, (float)PAUSE_BAR_WIDTH, 16.0f};
}

void DrawPauseOverlay(const Settings *settings, int selected, bool fullscreen)
{
    const int font_size = 28;
    int width = UiWidth(), height = UiHeight();
    rlPushMatrix();
    rlScalef(UiScale(), UiScale(), 1.0f);
    DrawRectangle(0, 0, width, height, (Color){0, 0, 0, 150});
    DrawTextCentered("PAUSED", width / 2, height / 3, 110, WHITE);

    for (int i = 0; i < SETTING_COUNT; i++) {
        const SettingDef *def = SettingsDef((SettingId)i);
        float value = SettingsGet(settings, (SettingId)i);
        float ratio = Clamp((value - def->min) / (def->max - def->min), 0.0f, 1.0f);
        Rectangle row = PauseRowBounds(i);
        Rectangle bar = PauseBarBounds(i);
        bool active = i == selected;
        Color color = active ? (Color){255, 230, 120, 255} : (Color){255, 255, 255, 190};
        DrawTextShadow(def->name, (int)row.x, (int)row.y, font_size, color);
        DrawRectangle((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height, (Color){0, 0, 0, 160});
        DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * ratio), (int)bar.height, color);
        DrawTextShadow(TextFormat(active ? "< %.*f >" : "%.*f", def->decimals, (double)value), (int)(bar.x + bar.width) + 20, (int)row.y,
                       font_size, color);
    }

    Rectangle display_row = PauseRowBounds(PAUSE_ROW_DISPLAY);
    Color display_color = selected == PAUSE_ROW_DISPLAY ? (Color){255, 230, 120, 255} : (Color){255, 255, 255, 190};
    DrawTextShadow("window mode", (int)display_row.x, (int)display_row.y, font_size, display_color);
    DrawTextShadow(TextFormat(selected == PAUSE_ROW_DISPLAY ? "< %s >" : "%s", fullscreen ? "fullscreen" : "windowed"),
                   (int)PauseBarBounds(PAUSE_ROW_DISPLAY).x, (int)display_row.y, font_size, display_color);

    DrawTextCentered("Mouse or Up/Down/Left/Right to adjust, Esc resume, F11 fullscreen, Q quit", width / 2,
                     (int)PauseRowBounds(PAUSE_ROW_COUNT).y + 30, 24, (Color){255, 230, 120, 255});
    rlPopMatrix();
}

#define DEBUG_ROW_HEIGHT 32
#define DEBUG_FONT_SIZE 24
#define DEBUG_TITLE_SIZE 56
#define DEBUG_TITLE_GAP 70
#define DEBUG_FOOTER_GAP 16
#define DEBUG_FOOTER_SIZE 20

/* The rows stay left-aligned, so the list is centered by its widest label. */
Rectangle DebugRowBounds(int row)
{
    int list_width = 0;
    for (int i = 0; i < DEBUG_COMMAND_COUNT; i++) list_width = (int)fmaxf((float)list_width, (float)MeasureText(DEBUG_COMMANDS[i].label, DEBUG_FONT_SIZE));
    int menu_height = DEBUG_TITLE_GAP + DEBUG_COMMAND_COUNT * DEBUG_ROW_HEIGHT + DEBUG_FOOTER_GAP + DEBUG_FOOTER_SIZE;
    int top = (UiHeight() - menu_height) / 2 + DEBUG_TITLE_GAP;
    return (Rectangle){(float)((UiWidth() - list_width) / 2), (float)(top + row * DEBUG_ROW_HEIGHT), (float)list_width,
                       (float)DEBUG_ROW_HEIGHT};
}

void DrawDebugOverlay(const GameState *g, int selected)
{
    int width = UiWidth(), height = UiHeight();
    rlPushMatrix();
    rlScalef(UiScale(), UiScale(), 1.0f);
    DrawRectangle(0, 0, width, height, (Color){0, 0, 0, 150});
    DrawTextCentered("DEBUG", width / 2, (int)DebugRowBounds(0).y - DEBUG_TITLE_GAP, DEBUG_TITLE_SIZE, WHITE);

    for (int i = 0; i < DEBUG_COMMAND_COUNT; i++) {
        Rectangle row = DebugRowBounds(i);
        Color color = i == selected ? (Color){255, 230, 120, 255} : (Color){255, 255, 255, 190};
        const char *label = DEBUG_COMMANDS[i].label;
        if (DEBUG_COMMANDS[i].kind == DEBUG_GOD_MODE) label = TextFormat("God mode: %s", g->god_mode ? "ON" : "OFF");
        DrawTextShadow(label, (int)row.x, (int)row.y, DEBUG_FONT_SIZE, color);
    }

    DrawTextCentered("Up/Down select, Enter or click apply, F1 or Esc close", width / 2,
                     (int)DebugRowBounds(DEBUG_COMMAND_COUNT).y + DEBUG_FOOTER_GAP, DEBUG_FOOTER_SIZE, (Color){255, 230, 120, 255});
    rlPopMatrix();
}
