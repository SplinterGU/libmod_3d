/*
 * libmod_3d_physics.c - collision + character controller (see header).
 */
#include "libmod_3d_physics.h"
#include "libmod_3d_scenefile.h"   /* g3d_scene_terrain_height */
#include "libmod_3d_math.h"        /* Quat + quaternion helpers (rigid body spin) */
#include <math.h>
#include <string.h>

#define MAX_CHARS 32
#define MAX_BOXES 512

#ifdef USE_JOLT
/* Level-mesh collision lives in the Jolt world (libmod_3d_jolt.cpp). The char
   controller queries it so it collides with geometry added by
   g3d_collider_add_mesh (e.g. Tomb Raider rooms), not just AABB boxes. When no
   mesh collider exists (heightmap demos) these are no-ops and nothing changes. */
extern int   g3d_jolt_mesh_count(void);
extern float g3d_jolt_ground_below(float x, float z, float y_top, float y_min);
extern int   g3d_jolt_slide_capsule(float *x, float *z, float feet,
                                     float radius, float height, float step);
#endif

typedef struct {
    float px, py, pz;        /* feet position (bottom-centre of the capsule)   */
    float vx, vy, vz;        /* velocity                                       */
    float want_x, want_z;    /* desired horizontal velocity (from input)       */
    float radius, height;
    float step;              /* max ledge height that can be climbed           */
    float slope_cos;         /* cos(max walkable slope); steeper -> slide       */
    int   grounded;
    int   active;
    int   in_water;          /* 1 = swimming (buoyancy instead of gravity)      */
    float water_y;           /* world Y of the water surface while in_water     */
    float swim_up;           /* extra upward stroke this frame (from jump key)  */
} G3DChar;

typedef struct { float mn[3], mx[3]; int active; } G3DBox;

static G3DChar g_chars[MAX_CHARS];
static G3DBox  g_boxes[MAX_BOXES];
static float   g_gravity = 24.0f;

/* ------------------------------------------------------------------------- */

int g3d_char_create(float x, float y, float z, float radius, float height) {
    for (int i = 0; i < MAX_CHARS; i++) {
        if (g_chars[i].active) continue;
        G3DChar *c = &g_chars[i];
        memset(c, 0, sizeof(*c));
        c->px = x; c->py = y; c->pz = z;
        c->radius = radius > 0.05f ? radius : 0.4f;
        c->height = height > 0.2f ? height : 1.8f;
        c->step = 0.5f;
        c->slope_cos = cosf(48.0f * 3.14159265f / 180.0f);
        c->active = 1;
        return i;
    }
    return -1;
}

void g3d_char_destroy(int id) {
    if (id >= 0 && id < MAX_CHARS) g_chars[id].active = 0;
}
void g3d_char_clear_all(void) {
    for (int i = 0; i < MAX_CHARS; i++) g_chars[i].active = 0;
}

void g3d_char_move(int id, float vx, float vz) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    g_chars[id].want_x = vx; g_chars[id].want_z = vz;
}

void g3d_char_jump(int id, float speed) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    if (c->in_water) { c->swim_up = speed; }        /* swim stroke upward */
    else if (c->grounded) { c->vy = speed; c->grounded = 0; }
}

/* Mark the character as in water (buoyancy + free vertical swim) or not, and give
   the water surface Y. Call every frame from the game once it knows Lara is in a
   pool (below a water surface, over its footprint). */
void g3d_char_set_water(int id, int in_water, float surface_y) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    g_chars[id].in_water = in_water ? 1 : 0;
    g_chars[id].water_y = surface_y;
}

void g3d_char_set_position(int id, float x, float y, float z) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    c->px = x; c->py = y; c->pz = z; c->vx = c->vy = c->vz = 0.0f;
}

void g3d_char_set_tuning(int id, float step, float slope_deg) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    if (step >= 0.0f) c->step = step;
    if (slope_deg > 1.0f && slope_deg < 89.0f)
        c->slope_cos = cosf(slope_deg * 3.14159265f / 180.0f);
}

float g3d_char_x(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].px : 0.0f; }
float g3d_char_y(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].py : 0.0f; }
float g3d_char_z(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].pz : 0.0f; }
int   g3d_char_grounded(int id) { return (id>=0 && id<MAX_CHARS && g_chars[id].active) ? g_chars[id].grounded : 0; }

#ifdef USE_JOLT
void g3d_jolt_set_gravity(float g);   /* forward to the Jolt backend */
#endif
void g3d_physics_set_gravity(float g) {
    g_gravity = g;                    /* char controller + vehicle (always custom) */
#ifdef USE_JOLT
    g3d_jolt_set_gravity(g);          /* rigid bodies (Jolt) */
#endif
}

int g3d_collider_add_box(float minx, float miny, float minz,
                         float maxx, float maxy, float maxz) {
    for (int i = 0; i < MAX_BOXES; i++) {
        if (g_boxes[i].active) continue;
        g_boxes[i].mn[0] = minx < maxx ? minx : maxx;
        g_boxes[i].mn[1] = miny < maxy ? miny : maxy;
        g_boxes[i].mn[2] = minz < maxz ? minz : maxz;
        g_boxes[i].mx[0] = minx > maxx ? minx : maxx;
        g_boxes[i].mx[1] = miny > maxy ? miny : maxy;
        g_boxes[i].mx[2] = minz > maxz ? minz : maxz;
        g_boxes[i].active = 1;
        return i;
    }
    return -1;
}
void g3d_collider_clear(void) {
    for (int i = 0; i < MAX_BOXES; i++) g_boxes[i].active = 0;
}

/* ========================================================================= */
/*  raycast vehicle (arcade: bicycle steering + 4-wheel terrain follow)       */
/* ========================================================================= */

#define MAX_VEHICLES 8

typedef struct {
    float px, py, pz;        /* chassis centre                                 */
    float yaw, pitch, roll;  /* heading + visual tilt (radians)                */
    float speed;             /* signed forward speed                           */
    float vy;                /* vertical velocity (jumps / air)                */
    float steer;             /* current steer angle (radians, smoothed)        */
    int   airborne;
    /* geometry + tuning */
    float wheelbase, track, ride;
    float engine, top_speed, max_steer, brake_force, drag;
    int   active;
} G3DVehicle;

static G3DVehicle g_veh[MAX_VEHICLES];

int g3d_vehicle_create(float x, float y, float z, float heading) {
    for (int i = 0; i < MAX_VEHICLES; i++) {
        if (g_veh[i].active) continue;
        G3DVehicle *v = &g_veh[i];
        memset(v, 0, sizeof(*v));
        v->px = x; v->py = y; v->pz = z; v->yaw = heading;
        v->wheelbase = 4.0f; v->track = 2.4f; v->ride = 1.0f;
        v->engine = 26.0f; v->top_speed = 55.0f;
        v->max_steer = 32.0f * 3.14159265f / 180.0f;
        v->brake_force = 40.0f; v->drag = 0.8f;
        v->active = 1;
        return i;
    }
    return -1;
}
void g3d_vehicle_destroy(int id) { if (id>=0 && id<MAX_VEHICLES) g_veh[id].active = 0; }

void g3d_vehicle_set_geometry(int id, float wheelbase, float track, float ride) {
    if (id<0 || id>=MAX_VEHICLES || !g_veh[id].active) return;
    G3DVehicle *v = &g_veh[id];
    if (wheelbase > 0.1f) v->wheelbase = wheelbase;
    if (track > 0.1f)     v->track = track;
    if (ride >= 0.0f)     v->ride = ride;
}
void g3d_vehicle_set_tuning(int id, float engine, float top_speed,
                            float max_steer_deg, float brake_force, float drag) {
    if (id<0 || id>=MAX_VEHICLES || !g_veh[id].active) return;
    G3DVehicle *v = &g_veh[id];
    if (engine > 0.0f)      v->engine = engine;
    if (top_speed > 0.0f)   v->top_speed = top_speed;
    if (max_steer_deg > 0.0f) v->max_steer = max_steer_deg * 3.14159265f / 180.0f;
    if (brake_force > 0.0f) v->brake_force = brake_force;
    if (drag > 0.0f)        v->drag = drag;
}

float g3d_vehicle_x(int id)     { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].px:0.0f; }
float g3d_vehicle_y(int id)     { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].py:0.0f; }
float g3d_vehicle_z(int id)     { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].pz:0.0f; }
float g3d_vehicle_yaw(int id)   { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].yaw:0.0f; }
float g3d_vehicle_pitch(int id) { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].pitch:0.0f; }
float g3d_vehicle_roll(int id)  { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].roll:0.0f; }
float g3d_vehicle_speed(int id) { return (id>=0&&id<MAX_VEHICLES&&g_veh[id].active)?g_veh[id].speed:0.0f; }

void g3d_vehicle_update(int id, float dt, float throttle, float steer_in, float brake) {
    if (id<0 || id>=MAX_VEHICLES || !g_veh[id].active) return;
    G3DVehicle *v = &g_veh[id];
    if (dt <= 0.0f) return; if (dt > 0.1f) dt = 0.1f;
    if (throttle < -1.0f) throttle = -1.0f; if (throttle > 1.0f) throttle = 1.0f;
    if (steer_in < -1.0f) steer_in = -1.0f; if (steer_in > 1.0f) steer_in = 1.0f;
    if (brake < 0.0f) brake = 0.0f; if (brake > 1.0f) brake = 1.0f;

    /* steering: ease toward target, less authority at high speed */
    float sp_frac = fabsf(v->speed) / (v->top_speed + 1e-3f);
    float target = steer_in * v->max_steer * (1.0f - 0.4f * sp_frac);
    float k = 8.0f * dt; if (k > 1.0f) k = 1.0f;
    v->steer += (target - v->steer) * k;

    /* engine + brake + drag (only drive when on the ground) */
    if (!v->airborne) v->speed += throttle * v->engine * dt;
    if (brake > 0.0f) {
        float b = brake * v->brake_force * dt;
        if (v->speed > 0.0f) v->speed = v->speed > b ? v->speed - b : 0.0f;
        else                 v->speed = v->speed < -b ? v->speed + b : 0.0f;
    }
    v->speed -= v->speed * v->drag * dt;                 /* aero + rolling drag */
    if (throttle == 0.0f && fabsf(v->speed) < 0.15f) v->speed = 0.0f;
    if (v->speed >  v->top_speed)        v->speed =  v->top_speed;
    if (v->speed < -v->top_speed * 0.4f) v->speed = -v->top_speed * 0.4f;

    /* heading via bicycle model (only steers while rolling on the ground) */
    if (!v->airborne && fabsf(v->speed) > 0.2f)
        v->yaw += (v->speed / v->wheelbase) * tanf(v->steer) * dt;

    float fwx = sinf(v->yaw), fwz = cosf(v->yaw);
    float rgx = cosf(v->yaw), rgz = -sinf(v->yaw);       /* body right (matches demo axes) */

    /* horizontal move along heading (full grip; drift is a later refinement) */
    v->px += fwx * v->speed * dt;
    v->pz += fwz * v->speed * dt;

    /* sample the ground under the four wheels */
    float hb = v->wheelbase * 0.5f, tr = v->track * 0.5f;
    float wx, wz;
    wx = v->px + fwx*hb - rgx*tr; wz = v->pz + fwz*hb - rgz*tr; float gFL = g3d_scene_terrain_height(wx, wz);
    wx = v->px + fwx*hb + rgx*tr; wz = v->pz + fwz*hb + rgz*tr; float gFR = g3d_scene_terrain_height(wx, wz);
    wx = v->px - fwx*hb - rgx*tr; wz = v->pz - fwz*hb - rgz*tr; float gRL = g3d_scene_terrain_height(wx, wz);
    wx = v->px - fwx*hb + rgx*tr; wz = v->pz - fwz*hb + rgz*tr; float gRR = g3d_scene_terrain_height(wx, wz);
    float front = (gFL + gFR) * 0.5f, rear = (gRL + gRR) * 0.5f;
    float left  = (gFL + gRL) * 0.5f, rightg = (gFR + gRR) * 0.5f;
    float avg   = (gFL + gFR + gRL + gRR) * 0.25f;
    float desired = avg + v->ride;

    /* vertical: glue to the ground while driving (no per-bump jitter); only go
       airborne off a real ledge or a strong ramp at speed. */
    if (!v->airborne) {
        float climb = (desired - v->py) / dt;            /* how fast the ground rises under us */
        if (desired < v->py - 0.6f) {                    /* drove off a ledge -> fall */
            v->airborne = 1; v->vy = 0.0f;
        } else if (climb > 8.0f && v->speed > 8.0f) {    /* real ramp at speed -> launch */
            v->airborne = 1; v->vy = climb * 0.5f;
        } else {
            v->py = desired; v->vy = 0.0f;               /* glued to the ground */
        }
    }
    if (v->airborne) {                                   /* ballistic until we land */
        v->vy -= g_gravity * dt;
        float ny = v->py + v->vy * dt;
        if (v->vy <= 0.0f && ny <= desired) { v->py = desired; v->vy = 0.0f; v->airborne = 0; }
        else v->py = ny;
    }

    /* visual tilt from the wheel heights (only meaningful on the ground) */
    float tp = atanf((rear - front) / v->wheelbase);
    float tr2 = atanf((rightg - left) / v->track);
    if (v->airborne) { tp = v->pitch; tr2 = v->roll; }   /* hold last tilt in the air */
    float ks = 8.0f * dt; if (ks > 1.0f) ks = 1.0f;
    v->pitch += (tp  - v->pitch) * ks;                   /* smooth so bumps don't jitter */
    v->roll  += (tr2 - v->roll)  * ks;
}

/* ------------------------------------------------------------------------- */

/* Push a vertical cylinder (centre cx,cz, radius r, y-span [y0,y1]) out of a
   box in the XZ plane. Returns 1 if it moved the cylinder. */
static int resolve_xz(const G3DBox *b, float *cx, float *cz, float r,
                      float y0, float y1) {
    if (y1 <= b->mn[1] || y0 >= b->mx[1]) return 0;          /* no vertical overlap */
    /* closest point on the box rectangle to the circle centre */
    float qx = *cx < b->mn[0] ? b->mn[0] : (*cx > b->mx[0] ? b->mx[0] : *cx);
    float qz = *cz < b->mn[2] ? b->mn[2] : (*cz > b->mx[2] ? b->mx[2] : *cz);
    float dx = *cx - qx, dz = *cz - qz;
    float d2 = dx*dx + dz*dz;
    if (d2 >= r*r) return 0;                                  /* no penetration */
    if (d2 > 1e-8f) {                                         /* outside: push along normal */
        float d = sqrtf(d2);
        *cx += dx / d * (r - d);
        *cz += dz / d * (r - d);
    } else {                                                  /* centre inside: push out shortest axis */
        float pxp = b->mx[0] - *cx, pxn = *cx - b->mn[0];
        float pzp = b->mx[2] - *cz, pzn = *cz - b->mn[2];
        float m = pxp; int axis = 0; float sign = 1.0f;
        if (pxn < m) { m = pxn; axis = 0; sign = -1.0f; }
        if (pzp < m) { m = pzp; axis = 2; sign = 1.0f; }
        if (pzn < m) { m = pzn; axis = 2; sign = -1.0f; }
        if (axis == 0) *cx += sign * (m + r); else *cz += sign * (m + r);
    }
    return 1;
}

/* Ground height under a cylinder footprint: terrain + tops of boxes we can stand
   on (top no higher than feet+step). */
static float ground_under(float cx, float cz, float r, float feet, float step) {
    float g = g3d_scene_terrain_height(cx, cz);
    for (int i = 0; i < MAX_BOXES; i++) {
        const G3DBox *b = &g_boxes[i];
        if (!b->active) continue;
        if (b->mx[1] > feet + step + 0.05f) continue;        /* too tall to step onto */
        /* footprint overlaps box in XZ (expanded by radius)? */
        float qx = cx < b->mn[0] ? b->mn[0] : (cx > b->mx[0] ? b->mx[0] : cx);
        float qz = cz < b->mn[2] ? b->mn[2] : (cz > b->mx[2] ? b->mx[2] : cz);
        float dx = cx - qx, dz = cz - qz;
        if (dx*dx + dz*dz > r*r) continue;
        if (b->mx[1] > g) g = b->mx[1];
    }
    return g;
}

void g3d_char_update(int id, float dt) {
    if (id < 0 || id >= MAX_CHARS || !g_chars[id].active) return;
    G3DChar *c = &g_chars[id];
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;

    /* ---- SWIMMING: buoyancy toward the surface + free horizontal control ------
       Replaces gravity/ground while in water. WASD moves in the plane, ESPACIO
       (swim_up) strokes up; otherwise she floats gently up to the surface so she
       can reach the edge and climb out. Still collides with pool walls (Jolt) and
       can't sink through the floor. */
    if (c->in_water) {
        c->vx = c->want_x; c->vz = c->want_z;         /* full horizontal control */

        float head = c->py + c->height;
        float target_vy;
        if (c->swim_up > 0.0f) target_vy = c->swim_up;          /* active stroke up */
        else if (head > c->water_y) target_vy = (c->water_y - head) * 4.0f; /* at surface: settle */
        else target_vy = 3.0f;                                   /* submerged: float up gently */
        c->vy += (target_vy - c->vy) * 6.0f * dt;                /* smooth */
        c->swim_up = 0.0f;

        float nxp = c->px + c->vx * dt;
        float nzp = c->pz + c->vz * dt;
#ifdef USE_JOLT
        if (g3d_jolt_mesh_count() > 0)
            g3d_jolt_slide_capsule(&nxp, &nzp, c->py, c->radius, c->height, c->step);
#endif
        for (int i = 0; i < MAX_BOXES; i++) {
            float y0 = c->py, y1 = c->py + c->height;
            if (g_boxes[i].active) resolve_xz(&g_boxes[i], &nxp, &nzp, c->radius, y0, y1);
        }
        c->px = nxp; c->pz = nzp;

        /* vertical, but never below the pool floor */
        float ny_new = c->py + c->vy * dt;
        float ground = ground_under(c->px, c->pz, c->radius, c->py, c->step);
#ifdef USE_JOLT
        if (g3d_jolt_mesh_count() > 0) {
            float gj = g3d_jolt_ground_below(c->px, c->pz, c->py + c->height, c->py - 4096.0f);
            if (gj > -1.0e29f) ground = gj;
        }
#endif
        if (ny_new < ground) { ny_new = ground; if (c->vy < 0.0f) c->vy = 0.0f; }
        c->py = ny_new;
        c->grounded = 0;
        return;
    }

    /* terrain normal at the current spot -> is this slope too steep to stand on? */
    float e = c->radius > 0.3f ? c->radius : 0.3f;
    float hL = g3d_scene_terrain_height(c->px - e, c->pz);
    float hR = g3d_scene_terrain_height(c->px + e, c->pz);
    float hD = g3d_scene_terrain_height(c->px, c->pz - e);
    float hU = g3d_scene_terrain_height(c->px, c->pz + e);
    float nx = hL - hR, nz = hD - hU, ny = 2.0f * e;
    float nlen = sqrtf(nx*nx + ny*ny + nz*nz); if (nlen < 1e-5f) nlen = 1.0f;
    float slope_y = ny / nlen;                                /* 1 = flat, small = steep */
    int steep = slope_y < c->slope_cos;

    /* horizontal control: full control on walkable ground, momentum + light
       air control otherwise, downhill slide on steep ground. */
    if (c->grounded && !steep) {
        c->vx = c->want_x; c->vz = c->want_z;
    } else {
        float k = 2.5f * dt; if (k > 1.0f) k = 1.0f;          /* light air control */
        c->vx += (c->want_x - c->vx) * k;
        c->vz += (c->want_z - c->vz) * k;
        if (c->grounded && steep) {                           /* slide downhill */
            float dgx = (hR - hL), dgz = (hU - hD);           /* downhill direction (XZ) */
            float dl = sqrtf(dgx*dgx + dgz*dgz);
            if (dl > 1e-5f) { c->vx += dgx/dl * g_gravity * dt; c->vz += dgz/dl * g_gravity * dt; }
        }
    }

    /* gravity */
    c->vy -= g_gravity * dt;

    /* --- horizontal integrate + collide-and-slide against boxes --- */
    float nxp = c->px + c->vx * dt;
    float nzp = c->pz + c->vz * dt;
    float y0 = c->py + c->step;                               /* ignore steppable low boxes here */
    float y1 = c->py + c->height;
    for (int i = 0; i < MAX_BOXES; i++) {
        if (!g_boxes[i].active) continue;
        if (resolve_xz(&g_boxes[i], &nxp, &nzp, c->radius, y0, y1)) {
            /* kill velocity into the wall so we slide along it, not through it */
            c->vx = (nxp - c->px) / dt;
            c->vz = (nzp - c->pz) / dt;
        }
    }
#ifdef USE_JOLT
    /* slide against level mesh walls (Tomb Raider rooms, etc.) */
    if (g3d_jolt_mesh_count() > 0 &&
        g3d_jolt_slide_capsule(&nxp, &nzp, c->py, c->radius, c->height, c->step)) {
        c->vx = (nxp - c->px) / dt;
        c->vz = (nzp - c->pz) / dt;
    }
#endif
    c->px = nxp; c->pz = nzp;

    /* --- vertical integrate + ground/ceiling --- */
    float ground = ground_under(c->px, c->pz, c->radius, c->py, c->step);
#ifdef USE_JOLT
    /* level mesh floor (overrides the heightmap when a level is loaded): ray
       straight down from the head finds the room floor under the character. */
    if (g3d_jolt_mesh_count() > 0) {
        float gj = g3d_jolt_ground_below(c->px, c->pz, c->py + c->height, c->py - 4096.0f);
        if (gj > -1.0e29f) ground = gj;
    }
#endif
    float ny_new = c->py + c->vy * dt;

    /* ground snap: if we were on the ground and are only gently leaving it
       (walking downhill or down a small step, not jumping), stick to the floor
       instead of launching into the air for a few frames. Without this, walking
       downslope makes `grounded` flicker every frame and the game keeps popping
       into its jump/fall animation. The snap distance scales with speed so a
       fast run down a slope still stays glued. */
    float horiz = sqrtf(c->vx * c->vx + c->vz * c->vz);
    float snap = c->step + horiz * dt * 1.5f + 0.15f;
    int was_grounded = c->grounded;

    if (c->vy <= 0.0f && ny_new <= ground) {                  /* landing / standing */
        c->py = ground;
        c->vy = 0.0f;
        c->grounded = !steep;                                 /* steep: keep sliding */
    } else if (was_grounded && !steep && c->vy <= 0.0f &&
               ny_new > ground && (c->py - ground) <= snap) { /* follow ground down */
        c->py = ground;
        c->vy = 0.0f;
        c->grounded = 1;
    } else {
        /* ceiling: head hits the underside of a box */
        float head0 = c->py + c->height, head1 = ny_new + c->height;
        for (int i = 0; i < MAX_BOXES; i++) {
            const G3DBox *b = &g_boxes[i];
            if (!b->active || c->vy <= 0.0f) continue;
            if (b->mn[1] < head1 && b->mn[1] >= head0) {
                float qx = c->px < b->mn[0] ? b->mn[0] : (c->px > b->mx[0] ? b->mx[0] : c->px);
                float qz = c->pz < b->mn[2] ? b->mn[2] : (c->pz > b->mx[2] ? b->mx[2] : c->pz);
                float dx = c->px - qx, dz = c->pz - qz;
                if (dx*dx + dz*dz <= c->radius*c->radius) {
                    ny_new = b->mn[1] - c->height; c->vy = 0.0f;
                }
            }
        }
        c->py = ny_new;
        c->grounded = 0;
    }
}

#ifndef USE_JOLT   /* Jolt backend (libmod_3d_jolt.cpp) replaces this whole section */
/* ========================================================================= */
/*  rigid bodies (AABB boxes: fall, stack, collide, get pushed)               */
/*  Reuses the same static box colliders (g_boxes), terrain and gravity.      */
/* ========================================================================= */

#define MAX_BODIES 256

typedef struct {
    float px, py, pz;        /* centre                                         */
    float vx, vy, vz;
    float hx, hy, hz;        /* half-extents (AABB, no rotation in this MVP)   */
    float inv_mass;          /* 0 = static / immovable                         */
    float restitution;       /* bounciness 0..1                                */
    float friction;          /* ground/contact damping 0..1                    */
    Quat  orient;            /* orientation (free tumble in the air)           */
    float avx, avy, avz;     /* angular velocity (rad/s)                       */
    float inv_inertia;       /* scalar inverse inertia (box approximation)     */
    float ox, oy, oz;        /* body-centre -> model-origin offset (render)    */
    int   grounded;
    int   active;
} G3DBody;

static G3DBody g_bodies[MAX_BODIES];

int g3d_rigidbody_create(float x, float y, float z,
                         float hx, float hy, float hz, float mass) {
    for (int i = 0; i < MAX_BODIES; i++) {
        if (g_bodies[i].active) continue;
        G3DBody *b = &g_bodies[i];
        memset(b, 0, sizeof(*b));
        b->px = x; b->py = y; b->pz = z;
        b->hx = hx > 0.01f ? hx : 0.5f;
        b->hy = hy > 0.01f ? hy : 0.5f;
        b->hz = hz > 0.01f ? hz : 0.5f;
        b->inv_mass = mass > 0.0f ? 1.0f / mass : 0.0f;
        b->restitution = 0.12f;
        b->friction = 0.55f;
        b->orient = quat_identity();
        b->inv_inertia = b->inv_mass > 0.0f
                       ? b->inv_mass * 2.5f / (b->hx*b->hx + b->hy*b->hy + b->hz*b->hz) : 0.0f;
        b->ox = 0.0f; b->oy = b->hy; b->oz = 0.0f;   /* default: bottom-origin model */
        b->active = 1;
        return i;
    }
    return -1;
}
void g3d_rigidbody_destroy(int id) { if (id>=0 && id<MAX_BODIES) g_bodies[id].active = 0; }
void g3d_rigidbody_clear(void) { for (int i=0;i<MAX_BODIES;i++) g_bodies[i].active = 0; }

void g3d_rigidbody_apply_impulse(int id, float ix, float iy, float iz) {
    if (id<0 || id>=MAX_BODIES || !g_bodies[id].active) return;
    G3DBody *b = &g_bodies[id];
    b->vx += ix * b->inv_mass; b->vy += iy * b->inv_mass; b->vz += iz * b->inv_mass;
    if (iy > 0.0f) b->grounded = 0;
}
void g3d_rigidbody_set_velocity(int id, float vx, float vy, float vz) {
    if (id<0 || id>=MAX_BODIES || !g_bodies[id].active) return;
    g_bodies[id].vx = vx; g_bodies[id].vy = vy; g_bodies[id].vz = vz;
}
void g3d_rigidbody_set_bounce(int id, float restitution, float friction) {
    if (id<0 || id>=MAX_BODIES || !g_bodies[id].active) return;
    if (restitution >= 0.0f) g_bodies[id].restitution = restitution > 1.0f ? 1.0f : restitution;
    if (friction >= 0.0f)    g_bodies[id].friction = friction > 1.0f ? 1.0f : friction;
}

float g3d_rigidbody_x(int id) { return (id>=0&&id<MAX_BODIES&&g_bodies[id].active)?g_bodies[id].px:0.0f; }
float g3d_rigidbody_y(int id) { return (id>=0&&id<MAX_BODIES&&g_bodies[id].active)?g_bodies[id].py:0.0f; }
float g3d_rigidbody_z(int id) { return (id>=0&&id<MAX_BODIES&&g_bodies[id].active)?g_bodies[id].pz:0.0f; }
int   g3d_rigidbody_grounded(int id) { return (id>=0&&id<MAX_BODIES&&g_bodies[id].active)?g_bodies[id].grounded:0; }

void g3d_rigidbody_set_angular_velocity(int id, float wx, float wy, float wz) {
    if (id<0 || id>=MAX_BODIES || !g_bodies[id].active) return;
    g_bodies[id].avx = wx; g_bodies[id].avy = wy; g_bodies[id].avz = wz;
    g_bodies[id].grounded = 0;
}
/* Orientation as Euler radians (feed to the model rotation). */
float g3d_rigidbody_angle_x(int id) { if(id<0||id>=MAX_BODIES||!g_bodies[id].active)return 0.0f; float p,y,r; quat_to_euler(g_bodies[id].orient,&p,&y,&r); return p; }
float g3d_rigidbody_angle_y(int id) { if(id<0||id>=MAX_BODIES||!g_bodies[id].active)return 0.0f; float p,y,r; quat_to_euler(g_bodies[id].orient,&p,&y,&r); return y; }
float g3d_rigidbody_angle_z(int id) { if(id<0||id>=MAX_BODIES||!g_bodies[id].active)return 0.0f; float p,y,r; quat_to_euler(g_bodies[id].orient,&p,&y,&r); return r; }

/* Placement for a bottom-origin model (feet at the model origin, e.g. loaded via
   g3d_model_spawn): centre - R*(0,hy,0). Puts the model's centre at the body
   centre so it rests on the ground AND tumbles about its centre. */
float g3d_rigidbody_render_x(int id) { if(id<0||id>=MAX_BODIES||!g_bodies[id].active)return 0.0f; G3DBody*b=&g_bodies[id]; Vec3 o=quat_rotate_vec3(b->orient, vec3_make(b->ox,b->oy,b->oz)); return b->px - o.x; }
float g3d_rigidbody_render_y(int id) { if(id<0||id>=MAX_BODIES||!g_bodies[id].active)return 0.0f; G3DBody*b=&g_bodies[id]; Vec3 o=quat_rotate_vec3(b->orient, vec3_make(b->ox,b->oy,b->oz)); return b->py - o.y; }
float g3d_rigidbody_render_z(int id) { if(id<0||id>=MAX_BODIES||!g_bodies[id].active)return 0.0f; G3DBody*b=&g_bodies[id]; Vec3 o=quat_rotate_vec3(b->orient, vec3_make(b->ox,b->oy,b->oz)); return b->pz - o.z; }

void g3d_rigidbody_set_model_offset(int id, float ox, float oy, float oz) {
    if (id<0 || id>=MAX_BODIES || !g_bodies[id].active) return;
    g_bodies[id].ox = ox; g_bodies[id].oy = oy; g_bodies[id].oz = oz;
}

/* World-aligned half-extents of the oriented box (its bounding box). Lets the
   collision follow the orientation, so a barrel on its side rests at its real
   height instead of floating. */
static void obb_world_half(const G3DBody *b, float *ha) {
    Vec3 ex = quat_rotate_vec3(b->orient, vec3_make(b->hx, 0.0f, 0.0f));
    Vec3 ey = quat_rotate_vec3(b->orient, vec3_make(0.0f, b->hy, 0.0f));
    Vec3 ez = quat_rotate_vec3(b->orient, vec3_make(0.0f, 0.0f, b->hz));
    ha[0] = fabsf(ex.x) + fabsf(ey.x) + fabsf(ez.x);
    ha[1] = fabsf(ex.y) + fabsf(ey.y) + fabsf(ez.y);
    ha[2] = fabsf(ex.z) + fabsf(ey.z) + fabsf(ez.z);
}

/* Separate body `a` (world half-extents `aha`) from an AABB [mn,mx] along the
   axis of least penetration. `share` = how much of the push a takes (1 vs a
   static box, inv-mass split vs another body). Returns the axis or -1. */
static int mtv_resolve(G3DBody *a, const float *aha, const float *mn, const float *mx,
                       float shareA, G3DBody *b, float shareB) {
    float amn[3] = { a->px-aha[0], a->py-aha[1], a->pz-aha[2] };
    float amx[3] = { a->px+aha[0], a->py+aha[1], a->pz+aha[2] };
    float ov[3];
    for (int k = 0; k < 3; k++) {
        float lo = amn[k] > mn[k] ? amn[k] : mn[k];
        float hi = amx[k] < mx[k] ? amx[k] : mx[k];
        ov[k] = hi - lo;
        if (ov[k] <= 0.0f) return -1;                 /* separated on this axis */
    }
    int axis = 0;                                     /* least-penetration axis */
    if (ov[1] < ov[axis]) axis = 1;
    if (ov[2] < ov[axis]) axis = 2;
    float acen = (axis==0)?a->px:(axis==1)?a->py:a->pz;
    float bcen = (mn[axis] + mx[axis]) * 0.5f;
    float sign = (acen < bcen) ? -1.0f : 1.0f;        /* push a away from b */
    float push = ov[axis];

    float da = sign * push * shareA;
    if (axis==0) a->px += da; else if (axis==1) a->py += da; else a->pz += da;
    if (b) { float db = -sign * push * shareB;
             if (axis==0) b->px += db; else if (axis==1) b->py += db; else b->pz += db; }
    return axis;
}

/* Bounce/kill the velocity component along the contact axis + tangential friction. */
static void response_axis(G3DBody *a, int axis, float rest, float fric) {
    float *va = (axis==0)?&a->vx:(axis==1)?&a->vy:&a->vz;
    if (*va != 0.0f) *va = -(*va) * rest;             /* reflect along normal */
    /* tangential friction */
    if (axis != 0) a->vx *= (1.0f - fric);
    if (axis != 1) a->vy *= (1.0f - fric);
    if (axis != 2) a->vz *= (1.0f - fric);
    if (axis == 1 && a->py >= 0.0f) a->grounded = 1;
}

void g3d_rigidbody_step(float dt) {
    if (dt <= 0.0f) return; if (dt > 0.05f) dt = 0.05f;

    /* integrate position + orientation (free tumble) */
    for (int i = 0; i < MAX_BODIES; i++) {
        G3DBody *b = &g_bodies[i];
        if (!b->active || b->inv_mass == 0.0f) continue;
        b->vy -= g_gravity * dt;
        b->px += b->vx * dt; b->py += b->vy * dt; b->pz += b->vz * dt;
        float wl = sqrtf(b->avx*b->avx + b->avy*b->avy + b->avz*b->avz);
        if (wl > 1e-5f) {
            Quat dq = quat_from_axis_angle(vec3_make(b->avx/wl, b->avy/wl, b->avz/wl), wl*dt);
            b->orient = quat_normalize(quat_multiply(dq, b->orient));
        }
        b->grounded = 0;
    }

    /* solve contacts (a few relaxation iterations for stable stacking) */
    for (int it = 0; it < 4; it++) {
        for (int i = 0; i < MAX_BODIES; i++) {
            G3DBody *b = &g_bodies[i];
            if (!b->active || b->inv_mass == 0.0f) continue;

            float ha[3]; obb_world_half(b, ha);           /* orientation-aware extents */

            /* vs terrain (ground under the box centre) */
            float g = g3d_scene_terrain_height(b->px, b->pz);
            if (b->py - ha[1] < g) {
                float impact = b->vy;
                b->py = g + ha[1];
                if (it == 0 && impact < -1.0f) {          /* tumble from horizontal motion on impact */
                    b->avx += -b->vz * 0.28f;
                    b->avz +=  b->vx * 0.28f;
                }
                if (b->vy < 0.0f) b->vy = -b->vy * b->restitution;
                if (fabsf(b->vy) < 0.6f) b->vy = 0.0f;
                float f = b->friction * dt * 8.0f; if (f > 0.9f) f = 0.9f;
                b->vx *= (1.0f - f); b->vz *= (1.0f - f);   /* ground friction */
                b->grounded = 1;
            }

            /* vs static box colliders (walls, platforms) */
            for (int k = 0; k < MAX_BOXES; k++) {
                if (!g_boxes[k].active) continue;
                int ax = mtv_resolve(b, ha, g_boxes[k].mn, g_boxes[k].mx, 1.0f, NULL, 0.0f);
                if (ax >= 0) response_axis(b, ax, b->restitution, b->friction * 0.5f);
            }

            /* vs other dynamic bodies */
            for (int j = i + 1; j < MAX_BODIES; j++) {
                G3DBody *o = &g_bodies[j];
                if (!o->active) continue;
                float imsum = b->inv_mass + o->inv_mass;
                if (imsum == 0.0f) continue;
                float oha[3]; obb_world_half(o, oha);
                float omn[3] = { o->px-oha[0], o->py-oha[1], o->pz-oha[2] };
                float omx[3] = { o->px+oha[0], o->py+oha[1], o->pz+oha[2] };
                float sa = b->inv_mass / imsum, sb = o->inv_mass / imsum;
                int ax = mtv_resolve(b, ha, omn, omx, sa, o, sb);
                if (ax >= 0) {
                    /* exchange velocity along the contact axis (equal-ish) */
                    float *vb = (ax==0)?&b->vx:(ax==1)?&b->vy:&b->vz;
                    float *vo = (ax==0)?&o->vx:(ax==1)?&o->vy:&o->vz;
                    float rest = (b->restitution + o->restitution) * 0.5f;
                    float rel = *vb - *vo;
                    if ((*vb - *vo) * ((ax==0?(b->px-o->px):ax==1?(b->py-o->py):(b->pz-o->pz))) < 0.0f) {
                        float imp = -(1.0f + rest) * rel / imsum;
                        *vb += imp * b->inv_mass;
                        *vo -= imp * o->inv_mass;
                    }
                    if (ax == 1) { if (b->py > o->py) b->grounded = 1; else o->grounded = 1; }
                }
            }
        }
    }

    /* angular damping + settle flat when resting (keeps stacks tidy, no fake
       continuous rolling: boxes tumble in the air, then come to rest aligned) */
    for (int i = 0; i < MAX_BODIES; i++) {
        G3DBody *b = &g_bodies[i];
        if (!b->active || b->inv_mass == 0.0f) continue;
        if (b->grounded) {
            float d = 1.0f - 12.0f*dt; if (d < 0.0f) d = 0.0f;
            b->avx *= d; b->avy *= d; b->avz *= d;
            float wl = sqrtf(b->avx*b->avx + b->avy*b->avy + b->avz*b->avz);
            if (wl < 5.0f) {
                /* Settle the box's "up" onto the NEAREST world axis: it can come
                   to rest upright OR on its side depending on how it landed (the
                   orientation-aware collision box keeps it flush either way). */
                Vec3 up = quat_rotate_vec3(b->orient, vec3_make(0.0f, 1.0f, 0.0f));
                float axm = fabsf(up.x), aym = fabsf(up.y), azm = fabsf(up.z);
                Vec3 t;
                if (aym >= axm && aym >= azm) t = vec3_make(0.0f, up.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                else if (axm >= azm)          t = vec3_make(up.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                else                          t = vec3_make(0.0f, 0.0f, up.z >= 0.0f ? 1.0f : -1.0f);
                float dp = up.x*t.x + up.y*t.y + up.z*t.z;
                if (dp < 0.99995f) {
                    Vec3 axis = vec3_cross(up, t);
                    float al = vec3_length(axis);
                    if (al < 1e-4f) { axis = vec3_make(1.0f, 0.0f, 0.0f); al = 1.0f; }
                    axis = vec3_make(axis.x/al, axis.y/al, axis.z/al);
                    float ang = acosf(dp < -1.0f ? -1.0f : (dp > 1.0f ? 1.0f : dp));
                    float k = 6.0f*dt; if (k > 1.0f) k = 1.0f;
                    b->orient = quat_normalize(quat_multiply(quat_from_axis_angle(axis, ang*k), b->orient));
                }
            }
        } else {
            float d = 1.0f - 0.25f*dt; b->avx *= d; b->avy *= d; b->avz *= d;
        }
        float wl = sqrtf(b->avx*b->avx + b->avy*b->avy + b->avz*b->avz);   /* clamp spin */
        if (wl > 24.0f) { float s = 24.0f/wl; b->avx*=s; b->avy*=s; b->avz*=s; }
    }
}

/* Tier-1 shapes/CCD/mesh-collider are Jolt-only. Provide fallbacks so the
   module still builds and links without USE_JOLT (sphere/capsule/cylinder map
   to the existing box body; convex/mesh degrade gracefully). */
int g3d_rigidbody_create_sphere(float x, float y, float z, float radius, float mass) {
    return g3d_rigidbody_create(x, y, z, radius, radius, radius, mass);
}
int g3d_rigidbody_create_capsule(float x, float y, float z, float radius, float half_height, float mass) {
    return g3d_rigidbody_create(x, y, z, radius, half_height + radius, radius, mass);
}
int g3d_rigidbody_create_cylinder(float x, float y, float z, float radius, float half_height, float mass) {
    return g3d_rigidbody_create(x, y, z, radius, half_height, radius, mass);
}
int g3d_rigidbody_create_convex(float x, float y, float z, void *model, int submesh, float scale, float mass) {
    (void)model; (void)submesh; (void)scale; (void)x; (void)y; (void)z; (void)mass;
    return -1;   /* no convex hulls without Jolt; caller falls back to a box */
}
void g3d_rigidbody_set_ccd(int id, int enabled) { (void)id; (void)enabled; }
int  g3d_collider_add_mesh(void *model, int submesh, float x, float y, float z, float scale) {
    (void)model; (void)submesh; (void)x; (void)y; (void)z; (void)scale; return -1;
}

#endif /* !USE_JOLT */
