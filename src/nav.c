#include "nav.h"

#include <math.h>
#include <stdlib.h>

#include "raymath.h"

#define NAV_SEED_RADIUS 2
#define NAV_SEED_REACH 0.9f

#define LOOKAHEAD_CELLS 3

static const int STEP_DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
static const int STEP_DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
#define STRAIGHT_COST 10
#define DIAGONAL_COST 14
static const uint16_t STEP_COST[8] = {STRAIGHT_COST, STRAIGHT_COST, STRAIGHT_COST, STRAIGHT_COST,
                                      DIAGONAL_COST, DIAGONAL_COST, DIAGONAL_COST, DIAGONAL_COST};

int NavCellAt(Vector3 pos)
{
    int col = (int)floorf((pos.x - NAV_MIN_X) / NAV_CELL_SIZE);
    int row = (int)floorf((pos.z - NAV_MIN_Z) / NAV_CELL_SIZE);
    col = col < 0 ? 0 : col >= NAV_COLS ? NAV_COLS - 1 : col;
    row = row < 0 ? 0 : row >= NAV_ROWS ? NAV_ROWS - 1 : row;
    return row * NAV_COLS + col;
}

Vector3 NavCellCenter(int cell)
{
    return (Vector3){NAV_MIN_X + ((float)(cell % NAV_COLS) + 0.5f) * NAV_CELL_SIZE, 0.0f,
                     NAV_MIN_Z + ((float)(cell / NAV_COLS) + 0.5f) * NAV_CELL_SIZE};
}

static int StepCell(int cell, int k)
{
    int col = cell % NAV_COLS + STEP_DX[k];
    int row = cell / NAV_COLS + STEP_DZ[k];
    if (col < 0 || col >= NAV_COLS || row < 0 || row >= NAV_ROWS) return -1;
    return row * NAV_COLS + col;
}

static bool CornerOpen(const bool *open, int cell, int dx, int dz)
{
    return open[cell + dx] && open[cell + dz * NAV_COLS];
}

static bool CornerClear(const NavField *f, int cell, int k)
{
    return k < 4 || CornerOpen(f->walkable, cell, STEP_DX[k], STEP_DZ[k]);
}

#define QUEUE_BUCKETS 64
_Static_assert(QUEUE_BUCKETS > (NAV_SEED_RADIUS + 1) * DIAGONAL_COST + DIAGONAL_COST, "every queued cost must fit in one turn of the buckets");

typedef struct CostQueue {
    int count;
    int head[QUEUE_BUCKETS];
    int next[NAV_CELLS];
    int prev[NAV_CELLS];
    bool queued[NAV_CELLS];
} CostQueue;

static void QueueUnlink(CostQueue *q, int bucket, int cell)
{
    if (q->prev[cell] >= 0) q->next[q->prev[cell]] = q->next[cell];
    else q->head[bucket] = q->next[cell];
    if (q->next[cell] >= 0) q->prev[q->next[cell]] = q->prev[cell];
    q->queued[cell] = false;
    q->count--;
}

static void QueueLower(CostQueue *q, uint16_t *cost, int cell, uint16_t value)
{
    if (q->queued[cell]) QueueUnlink(q, cost[cell] % QUEUE_BUCKETS, cell);
    cost[cell] = value;
    int bucket = value % QUEUE_BUCKETS;
    q->prev[cell] = -1;
    q->next[cell] = q->head[bucket];
    if (q->head[bucket] >= 0) q->prev[q->head[bucket]] = cell;
    q->head[bucket] = cell;
    q->queued[cell] = true;
    q->count++;
}

static bool LineOpen(const bool *open, Vector3 from, Vector3 to)
{
    int cell = NavCellAt(from);
    int col = cell % NAV_COLS;
    int row = cell / NAV_COLS;
    float dx = to.x - from.x;
    float dz = to.z - from.z;
    if (dx < 0.0f && NAV_MIN_X + (float)col * NAV_CELL_SIZE == from.x) col--;
    if (dz < 0.0f && NAV_MIN_Z + (float)row * NAV_CELL_SIZE == from.z) row--;
    if (col < 0 || row < 0) return false;
    int end = NavCellAt(to);
    int steps = abs(end % NAV_COLS - col) + abs(end / NAV_COLS - row);
    int step_col = dx > 0.0f ? 1 : -1;
    int step_row = dz > 0.0f ? 1 : -1;
    float delta_x = dx != 0.0f ? NAV_CELL_SIZE / fabsf(dx) : INFINITY;
    float delta_z = dz != 0.0f ? NAV_CELL_SIZE / fabsf(dz) : INFINITY;
    float edge_x = NAV_MIN_X + (float)(col + (step_col > 0)) * NAV_CELL_SIZE;
    float edge_z = NAV_MIN_Z + (float)(row + (step_row > 0)) * NAV_CELL_SIZE;
    float next_x = dx != 0.0f ? (edge_x - from.x) / dx : INFINITY;
    float next_z = dz != 0.0f ? (edge_z - from.z) / dz : INFINITY;
    while (steps > 0) {
        bool cross_col = next_x <= next_z;
        bool cross_row = next_z <= next_x;
        int corner = row * NAV_COLS + col;
        if (cross_col) {
            col += step_col;
            next_x += delta_x;
            steps--;
        }
        if (cross_row) {
            row += step_row;
            next_z += delta_z;
            steps--;
        }
        if (col < 0 || col >= NAV_COLS || row < 0 || row >= NAV_ROWS) return false;
        if (cross_col && cross_row && !CornerOpen(open, corner, step_col, step_row)) return false;
        if (!open[row * NAV_COLS + col]) return false;
    }
    return row * NAV_COLS + col == end;
}

void NavFlood(NavField *f, Vector3 target)
{
    int goal = NavCellAt(target);
    CostQueue q;
    q.count = 0;
    for (int b = 0; b < QUEUE_BUCKETS; b++) q.head[b] = -1;
    for (int c = 0; c < NAV_CELLS; c++) {
        f->cost[c] = NAV_UNREACHED;
        q.queued[c] = false;
    }
    for (int dz = -NAV_SEED_RADIUS; dz <= NAV_SEED_RADIUS; dz++) {
        for (int dx = -NAV_SEED_RADIUS; dx <= NAV_SEED_RADIUS; dx++) {
            int col = goal % NAV_COLS + dx;
            int row = goal / NAV_COLS + dz;
            if (col < 0 || col >= NAV_COLS || row < 0 || row >= NAV_ROWS) continue;
            int c = row * NAV_COLS + col;
            float distance = Vector3Distance(NavCellCenter(c), target);
            if (distance > NAV_SEED_REACH || !f->walkable[c] || !LineOpen(f->solid_free, target, NavCellCenter(c))) continue;
            QueueLower(&q, f->cost, c, (uint16_t)(distance / NAV_CELL_SIZE * STRAIGHT_COST));
        }
    }
    for (int bucket = 0; q.count > 0; bucket = (bucket + 1) % QUEUE_BUCKETS) {
        while (q.head[bucket] >= 0) {
            int cell = q.head[bucket];
            QueueUnlink(&q, bucket, cell);
            for (int k = 0; k < 8; k++) {
                int next = StepCell(cell, k);
                if (next < 0 || !f->walkable[next] || !CornerClear(f, cell, k)) continue;
                uint16_t through = (uint16_t)(f->cost[cell] + STEP_COST[k]);
                if (through < f->cost[next]) QueueLower(&q, f->cost, next, through);
            }
        }
    }
    f->goal = goal;
}

static int Downhill(const NavField *f, int cell)
{
    int best = cell;
    uint16_t best_cost = f->walkable[cell] ? f->cost[cell] : NAV_UNREACHED;
    for (int k = 0; k < 8; k++) {
        int next = StepCell(cell, k);
        if (next < 0 || !f->walkable[next]) continue;
        if (f->walkable[cell] && !CornerClear(f, cell, k)) continue;
        if (f->cost[next] < best_cost) {
            best_cost = f->cost[next];
            best = next;
        }
    }
    return best;
}

Vector3 NavDirection(const NavField *f, Vector3 from, Vector3 target)
{
    int here = NavCellAt(from);
    int aim = Downhill(f, here);
    if (here == f->goal || aim == here) return Vector3Normalize(Vector3Subtract(target, from));
    for (int k = 0; k < LOOKAHEAD_CELLS && aim != f->goal; k++) {
        int further = Downhill(f, aim);
        if (further == aim || !LineOpen(f->walkable, from, NavCellCenter(further))) break;
        aim = further;
    }
    return Vector3Normalize(Vector3Subtract(NavCellCenter(aim), from));
}
