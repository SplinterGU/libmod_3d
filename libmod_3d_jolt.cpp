/*
 * libmod_3d_jolt.cpp - OPTIONAL Jolt Physics backend for the rigid body API.
 *
 * Compiled only when the module is built with -DUSE_JOLT. It implements the same
 * g3d_rigidbody_* C API as libmod_3d_physics.c (whose rigid body section is then
 * #ifndef USE_JOLT'd out), so the BGD scripts and demos are identical either way.
 *
 * Backed by Jolt Physics (github.com/jrouwe/JoltPhysics, MIT). The scene terrain
 * heightmap is fed to Jolt as a HeightFieldShape so bodies collide with it.
 */
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <thread>
#include <cstring>
#include <cmath>

/* Engine C API (rigid body decls + heightmap). NOTE: we deliberately do NOT
   include libmod_3d_math.h here — its Vec3/Quat clash with Jolt's. The one
   quaternion->Euler conversion we need is reimplemented below to match. */
extern "C" {
#include "libmod_3d_physics.h"
int g3d_scene_heightfield(const float **H, int *side, float *world_size);
}
#include <vector>

using namespace JPH;

/* ---- collision layers (static vs dynamic) ---- */
namespace Layers { static constexpr ObjectLayer NON_MOVING = 0, MOVING = 1, NUM = 2; }
namespace BPL     { static constexpr BroadPhaseLayer NON_MOVING(0), MOVING(1); static constexpr uint NUM = 2; }

class BPImpl final : public BroadPhaseLayerInterface {
    BroadPhaseLayer m[Layers::NUM];
public:
    BPImpl() { m[Layers::NON_MOVING] = BPL::NON_MOVING; m[Layers::MOVING] = BPL::MOVING; }
    uint GetNumBroadPhaseLayers() const override { return BPL::NUM; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return m[l]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "l"; }
#endif
};
class OvBImpl : public ObjectVsBroadPhaseLayerFilter {
public: bool ShouldCollide(ObjectLayer o, BroadPhaseLayer b) const override {
    return o == Layers::NON_MOVING ? b == BPL::MOVING : true; } };
class OLPImpl : public ObjectLayerPairFilter {
public: bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
    return a == Layers::NON_MOVING ? b == Layers::MOVING : true; } };

/* ---- global world ---- */
#define JRB_MAX 4096
static PhysicsSystem     *g_ps   = nullptr;
static TempAllocatorImpl *g_temp = nullptr;
static JobSystemThreadPool *g_job = nullptr;
static BPImpl  g_bp;
static OvBImpl g_ovb;
static OLPImpl g_olp;
static bool    g_inited   = false;
static bool    g_terrain_added = false;
static float   g_gravity  = 24.0f;

struct JRBody { BodyID id; float hx, hy, hz; float ox, oy, oz; int active; };
static JRBody g_rb[JRB_MAX];
static int    g_mesh_count = 0;   /* # of static trimesh colliders (level geometry) */

static void jolt_init() {
    if (g_inited) return;
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    g_temp = new TempAllocatorImpl(24 * 1024 * 1024);
    int nthreads = (int)std::thread::hardware_concurrency() - 1; if (nthreads < 1) nthreads = 1;
    g_job = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, nthreads);
    g_ps  = new PhysicsSystem();
    g_ps->Init(8192, 0, 16384, 8192, g_bp, g_ovb, g_olp);
    g_ps->SetGravity(Vec3(0.0f, -g_gravity, 0.0f));
    g_inited = true;
}

/* Build the scene heightmap as a static HeightFieldShape (cropped to 256, a
   multiple of Jolt's block size). Called lazily on the first step. */
static void jolt_add_terrain() {
    if (g_terrain_added) return;
    g_terrain_added = true;
    const float *H = nullptr; int side = 0; float ws = 0.0f;
    if (!g3d_scene_heightfield(&H, &side, &ws) || side < 2) return;

    const int N = (side - 1 < 256) ? side - 1 : 256;   /* samples per side (<=256) */
    float *samples = (float *)malloc(sizeof(float) * N * N);
    if (!samples) return;
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++)
            samples[iz * N + ix] = H[iz * side + ix];

    float cell = ws / (float)(side - 1);
    HeightFieldShapeSettings hs(samples, Vec3(-ws * 0.5f, 0.0f, -ws * 0.5f),
                                Vec3(cell, 1.0f, cell), (uint32)N);
    ShapeSettings::ShapeResult r = hs.Create();
    free(samples);
    if (r.HasError()) return;

    BodyCreationSettings bcs(r.Get(), RVec3::sZero(), Quat::sIdentity(),
                             EMotionType::Static, Layers::NON_MOVING);
    bcs.mFriction = 0.6f;
    g_ps->GetBodyInterface().CreateAndAddBody(bcs, EActivation::DontActivate);
    g_ps->OptimizeBroadPhase();
}

static int jrb_slot() { for (int i = 0; i < JRB_MAX; i++) if (!g_rb[i].active) return i; return -1; }
static inline bool jrb_ok(int id) { return id >= 0 && id < JRB_MAX && g_rb[id].active; }

/* ============================ C API ==================================== */
extern "C" {

/* Called by g3d_physics_set_gravity (which lives in libmod_3d_physics.c and also
   drives the char controller / vehicle). */
void g3d_jolt_set_gravity(float g) {
    g_gravity = g;
    if (g_ps) g_ps->SetGravity(Vec3(0.0f, -g, 0.0f));
}

/* Create a dynamic body from a ready-made shape and register it in a slot. The
   default model offset (0, oy, 0) suits a bottom-origin model; callers override
   with g3d_rigidbody_set_model_offset. NOTE: callers MUST have run jolt_init()
   before constructing `shape` (shapes need Jolt's types registered first). */
static int jrb_add_dynamic(Shape *shape, float x, float y, float z, float mass,
                           float hx, float hy, float hz, float oy) {
    int s = jrb_slot();
    if (s < 0) return -1;
    BodyCreationSettings bcs(shape, RVec3(x, y, z), Quat::sIdentity(),
                             EMotionType::Dynamic, Layers::MOVING);
    bcs.mRestitution = 0.12f;
    bcs.mFriction    = 0.55f;
    if (mass > 0.0f) {
        bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = mass;
    }
    BodyID bid = g_ps->GetBodyInterface().CreateAndAddBody(bcs, EActivation::Activate);
    g_rb[s].id = bid; g_rb[s].hx = hx; g_rb[s].hy = hy; g_rb[s].hz = hz;
    g_rb[s].ox = 0.0f; g_rb[s].oy = oy; g_rb[s].oz = 0.0f;
    g_rb[s].active = 1;
    return s;
}

int g3d_rigidbody_create(float x, float y, float z,
                         float hx, float hy, float hz, float mass) {
    jolt_init();   /* must precede shape construction (Jolt types registered) */
    if (hx < 0.01f) hx = 0.5f; if (hy < 0.01f) hy = 0.5f; if (hz < 0.01f) hz = 0.5f;
    return jrb_add_dynamic(new BoxShape(Vec3(hx, hy, hz)), x, y, z, mass, hx, hy, hz, hy);
}

int g3d_rigidbody_create_sphere(float x, float y, float z, float radius, float mass) {
    jolt_init();
    if (radius < 0.01f) radius = 0.5f;
    return jrb_add_dynamic(new SphereShape(radius), x, y, z, mass,
                           radius, radius, radius, radius);
}

int g3d_rigidbody_create_capsule(float x, float y, float z,
                                 float radius, float half_height, float mass) {
    jolt_init();
    if (radius < 0.01f) radius = 0.5f; if (half_height < 0.01f) half_height = 0.5f;
    /* Jolt capsule: half-height of the CYLINDER part; total half-height = hh+r */
    return jrb_add_dynamic(new CapsuleShape(half_height, radius), x, y, z, mass,
                           radius, half_height + radius, radius, half_height + radius);
}

int g3d_rigidbody_create_cylinder(float x, float y, float z,
                                  float radius, float half_height, float mass) {
    jolt_init();
    if (radius < 0.01f) radius = 0.5f; if (half_height < 0.01f) half_height = 0.5f;
    return jrb_add_dynamic(new CylinderShape(half_height, radius), x, y, z, mass,
                           radius, half_height, radius, half_height);
}

int g3d_rigidbody_create_convex(float x, float y, float z,
                                void *model, int submesh, float scale, float mass) {
    jolt_init();
    const float *pos = nullptr; int stride = 0;
    int vcount = g3d_physics_submesh_geom(model, submesh, &pos, &stride, nullptr, nullptr);
    if (vcount < 4 || !pos) return -1;   /* need a real hull */
    if (scale <= 0.0f) scale = 1.0f;

    /* Centroid (in model space) so the hull is built around the chunk centre;
       the body sits at world centroid and rotates about it, like the box path. */
    float cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < vcount; i++) {
        cx += pos[i*stride + 0]; cy += pos[i*stride + 1]; cz += pos[i*stride + 2];
    }
    cx /= vcount; cy /= vcount; cz /= vcount;

    Array<Vec3> pts; pts.reserve(vcount);
    for (int i = 0; i < vcount; i++)
        pts.push_back(Vec3((pos[i*stride+0]-cx)*scale,
                           (pos[i*stride+1]-cy)*scale,
                           (pos[i*stride+2]-cz)*scale));

    ConvexHullShapeSettings hs(pts);
    ShapeSettings::ShapeResult r = hs.Create();
    if (r.HasError()) return -1;

    int s = jrb_add_dynamic(r.Get().GetPtr(), x + cx*scale, y + cy*scale, z + cz*scale,
                            mass, 0.5f, 0.5f, 0.5f, 0.0f);
    if (s >= 0) { g_rb[s].ox = cx*scale; g_rb[s].oy = cy*scale; g_rb[s].oz = cz*scale; }
    return s;
}

void g3d_rigidbody_set_ccd(int id, int enabled) {
    if (!jrb_ok(id)) return;
    g_ps->GetBodyInterface().SetMotionQuality(
        g_rb[id].id, enabled ? EMotionQuality::LinearCast : EMotionQuality::Discrete);
}

/* Static triangle-mesh collider from a model submesh (level geometry). */
int g3d_collider_add_mesh(void *model, int submesh, float x, float y, float z, float scale) {
    jolt_init();
    const float *pos = nullptr; const unsigned int *idx = nullptr;
    int stride = 0, icount = 0;
    int vcount = g3d_physics_submesh_geom(model, submesh, &pos, &stride, &idx, &icount);
    if (vcount < 3 || icount < 3 || !pos || !idx) {
        printf("G3D jolt: add_mesh submesh %d FALLA (v=%d i=%d pos=%p idx=%p)\n",
               submesh, vcount, icount, (void*)pos, (void*)idx);
        return -1;
    }
    if (scale <= 0.0f) scale = 1.0f;

    VertexList verts; verts.reserve(vcount);
    for (int i = 0; i < vcount; i++)
        verts.push_back(Float3(pos[i*stride+0]*scale, pos[i*stride+1]*scale, pos[i*stride+2]*scale));
    IndexedTriangleList tris; tris.reserve(icount / 3);
    for (int i = 0; i + 2 < icount; i += 3)
        tris.push_back(IndexedTriangle(idx[i], idx[i+1], idx[i+2], 0));

    MeshShapeSettings ms(verts, tris);
    ShapeSettings::ShapeResult r = ms.Create();
    if (r.HasError()) return -1;

    BodyCreationSettings bcs(r.Get(), RVec3(x, y, z), Quat::sIdentity(),
                             EMotionType::Static, Layers::NON_MOVING);
    bcs.mFriction = 0.6f;
    BodyInterface &bi = g_ps->GetBodyInterface();
    BodyID bid = bi.CreateAndAddBody(bcs, EActivation::DontActivate);
    g_ps->OptimizeBroadPhase();
    int s = jrb_slot();
    if (s < 0) return -1;
    g_rb[s].id = bid; g_rb[s].hx = g_rb[s].hy = g_rb[s].hz = 0.0f;
    g_rb[s].ox = g_rb[s].oy = g_rb[s].oz = 0.0f; g_rb[s].active = 1;
    g_mesh_count++;
    return s;
}

void g3d_rigidbody_destroy(int id) {
    if (!jrb_ok(id)) return;
    BodyInterface &bi = g_ps->GetBodyInterface();
    bi.RemoveBody(g_rb[id].id); bi.DestroyBody(g_rb[id].id);
    g_rb[id].active = 0;
}
void g3d_rigidbody_clear(void) { for (int i = 0; i < JRB_MAX; i++) g3d_rigidbody_destroy(i); g_mesh_count = 0; }

void g3d_rigidbody_step(float dt) {
    if (!g_inited) return;
    jolt_add_terrain();
    if (dt <= 0.0f) return; if (dt > 0.05f) dt = 0.05f;
    g_ps->Update(dt, 1, g_temp, g_job);
}

void g3d_rigidbody_apply_impulse(int id, float ix, float iy, float iz) {
    if (!jrb_ok(id)) return;
    g_ps->GetBodyInterface().AddImpulse(g_rb[id].id, Vec3(ix, iy, iz));
}
void g3d_rigidbody_set_velocity(int id, float vx, float vy, float vz) {
    if (!jrb_ok(id)) return;
    g_ps->GetBodyInterface().SetLinearVelocity(g_rb[id].id, Vec3(vx, vy, vz));
}
void g3d_rigidbody_set_angular_velocity(int id, float wx, float wy, float wz) {
    if (!jrb_ok(id)) return;
    g_ps->GetBodyInterface().SetAngularVelocity(g_rb[id].id, Vec3(wx, wy, wz));
}
void g3d_rigidbody_set_bounce(int id, float restitution, float friction) {
    if (!jrb_ok(id)) return;
    BodyInterface &bi = g_ps->GetBodyInterface();
    if (restitution >= 0.0f) bi.SetRestitution(g_rb[id].id, restitution);
    if (friction >= 0.0f)    bi.SetFriction(g_rb[id].id, friction);
}
/* offset from body centre to the model's origin (for render_*). default (0,hy,0). */
void g3d_rigidbody_set_model_offset(int id, float ox, float oy, float oz) {
    if (!jrb_ok(id)) return;
    g_rb[id].ox = ox; g_rb[id].oy = oy; g_rb[id].oz = oz;
}

static RVec3 jrb_pos(int id) { return g_ps->GetBodyInterface().GetPosition(g_rb[id].id); }
static Quat  jrb_rot(int id) { return g_ps->GetBodyInterface().GetRotation(g_rb[id].id); }

float g3d_rigidbody_x(int id) { return jrb_ok(id) ? (float)jrb_pos(id).GetX() : 0.0f; }
float g3d_rigidbody_y(int id) { return jrb_ok(id) ? (float)jrb_pos(id).GetY() : 0.0f; }
float g3d_rigidbody_z(int id) { return jrb_ok(id) ? (float)jrb_pos(id).GetZ() : 0.0f; }
int   g3d_rigidbody_grounded(int id) {
    if (!jrb_ok(id)) return 0;
    return g_ps->GetBodyInterface().IsActive(g_rb[id].id) ? 0 : 1;   /* asleep ~ at rest */
}

/* orientation as engine Euler (replicates libmod_3d_math.c quat_to_euler so the
   result matches g3d_entity_set_rotation exactly, like the custom backend) */
static void jrb_euler(int id, float *pitch, float *yaw, float *roll) {
    Quat q = jrb_rot(id);
    float x = q.GetX(), y = q.GetY(), z = q.GetZ(), w = q.GetW();
    *roll  = std::atan2(2.0f*(w*x + y*z), 1.0f - 2.0f*(x*x + y*y));
    float sinp = 2.0f*(w*y - z*x);
    *pitch = std::fabs(sinp) >= 1.0f ? std::copysign(1.57079633f, sinp) : std::asin(sinp);
    *yaw   = std::atan2(2.0f*(w*z + x*y), 1.0f - 2.0f*(y*y + z*z));
}
float g3d_rigidbody_angle_x(int id) { if(!jrb_ok(id))return 0.0f; float p,y,r; jrb_euler(id,&p,&y,&r); return p; }
float g3d_rigidbody_angle_y(int id) { if(!jrb_ok(id))return 0.0f; float p,y,r; jrb_euler(id,&p,&y,&r); return y; }
float g3d_rigidbody_angle_z(int id) { if(!jrb_ok(id))return 0.0f; float p,y,r; jrb_euler(id,&p,&y,&r); return r; }

/* place a bottom-origin model centred on the body: centre - R*(0,hy,0) */
static Vec3 jrb_render(int id) {
    RVec3 p = jrb_pos(id);
    Vec3 off = jrb_rot(id) * Vec3(g_rb[id].ox, g_rb[id].oy, g_rb[id].oz);
    return Vec3((float)p.GetX() - off.GetX(), (float)p.GetY() - off.GetY(), (float)p.GetZ() - off.GetZ());
}
float g3d_rigidbody_render_x(int id) { return jrb_ok(id) ? jrb_render(id).GetX() : 0.0f; }
float g3d_rigidbody_render_y(int id) { return jrb_ok(id) ? jrb_render(id).GetY() : 0.0f; }
float g3d_rigidbody_render_z(int id) { return jrb_ok(id) ? jrb_render(id).GetZ() : 0.0f; }

/* ========================================================================= */
/*  Character-controller queries against the Jolt world (used by the custom   */
/*  capsule controller in libmod_3d_physics.c so it collides with level mesh  */
/*  colliders added via g3d_collider_add_mesh). No world Update() is needed -  */
/*  these are narrow-phase queries against the static bodies already present.  */
/* ========================================================================= */

/* How many static trimesh colliders (level geometry) exist. The char only
   consults Jolt when this is > 0, so scenes that never call add_mesh (all the
   heightmap demos) behave exactly as before. */
int g3d_jolt_mesh_count(void) { return g_mesh_count; }

/* Highest solid Y directly under (x,z), searching down from y_top to y_min.
   Returns -1e30 if the ray hits nothing (open air below the character). */
float g3d_jolt_ground_below(float x, float z, float y_top, float y_min) {
    if (!g_inited || !g_ps || g_mesh_count <= 0) return -1e30f;
    RVec3 from(x, y_top, z);
    Vec3  dir(0.0f, y_min - y_top, 0.0f);              /* straight down */
    RRayCast ray{ from, dir };
    RayCastResult hit;
    if (g_ps->GetNarrowPhaseQuery().CastRay(ray, hit))
        return (float)from.GetY() + hit.mFraction * (float)dir.GetY();
    return -1e30f;
}

/* Push a vertical capsule (feet..feet+height, ignoring the low `step` part so
   small ledges are climbable, not walls) out of the level walls. Moves (*x,*z)
   in XZ and returns 1 if it had to. Floor/ceiling contacts (near-vertical
   penetration axis) are left to the ground query. */
int g3d_jolt_slide_capsule(float *x, float *z, float feet,
                           float radius, float height, float step) {
    if (!g_inited || !g_ps || g_mesh_count <= 0) return 0;
    if (radius < 0.02f) radius = 0.02f;
    float bottom = feet + (step > 0.0f ? step : 0.0f);
    float top    = feet + height;
    if (top - bottom < 2.0f * radius + 0.04f) top = bottom + 2.0f * radius + 0.04f;
    float halfcyl = (top - bottom) * 0.5f - radius; if (halfcyl < 0.02f) halfcyl = 0.02f;
    float cy = (bottom + top) * 0.5f;

    /* heap Ref: Jolt shapes are ref-counted; a stack shape (refcount 0) can make
       the narrow-phase query misbehave. */
    RefConst<Shape> cap = new CapsuleShape(halfcyl, radius);
    RMat44 xf = RMat44::sTranslation(RVec3(*x, cy, *z));
    CollideShapeSettings s;
    s.mBackFaceMode = EBackFaceMode::CollideWithBackFaces;   /* TR walls are 1-sided */
    AllHitCollisionCollector<CollideShapeCollector> col;
    g_ps->GetNarrowPhaseQuery().CollideShape(cap, Vec3::sReplicate(1.0f), xf, s,
                                             RVec3::sZero(), col);
    if (col.mHits.empty()) return 0;

    /* resolve the single deepest wall contact (corners settle over a few frames) */
    float bx = 0.0f, bz = 0.0f, bd = 0.0f;
    for (const CollideShapeResult &h : col.mHits) {
        Vec3 ax = h.mPenetrationAxis.NormalizedOr(Vec3::sZero());
        float d = h.mPenetrationDepth;
        if (fabsf(ax.GetY()) > 0.7f) continue;             /* floor/ceiling, not a wall */
        if (d <= bd) continue;
        bd = d; bx = -ax.GetX() * d; bz = -ax.GetZ() * d;  /* move capsule out */
    }
    if (bd <= 0.0f) return 0;
    *x += bx; *z += bz;
    return 1;
}

} /* extern "C" */
