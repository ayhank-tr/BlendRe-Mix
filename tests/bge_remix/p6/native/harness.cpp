/* BGE-Remix P6: native checks of the PhysX backend (CPU), without Blender.
 * Each test builds a small world through the same calls the game engine
 * makes, runs it at 60 Hz and compares with what Bullet does. */
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "PxPhysicsEnvironment.h"
#include "PxPhysicsController.h"
#include "PxMotionState.h"
#include "PxShapes.h"
#include "PxSystem.h"
#include "PHY_IVehicle.h"
#include "PHY_ICharacter.h"
#include "CcdPhysicsController.h"
#include "PxPhysicsAPI.h"

static int g_fail = 0, g_pass = 0;
static std::string g_test;

#define CHECK(cond, ...)                                       \
	do {                                                         \
		char _m[512];                                              \
		snprintf(_m, sizeof(_m), __VA_ARGS__);                     \
		if (cond) {                                                \
			g_pass++;                                                \
			printf("  ok   %s\n", _m);                               \
		}                                                          \
		else {                                                     \
			g_fail++;                                                \
			printf("  FAIL %s  [%s]\n", _m, #cond);                  \
		}                                                          \
	} while (0)

/* ------------------------------------------------------------------------- */

struct World {
	PxPhysicsEnvironment *env;
	std::vector<PxPhysicsController *> ctrls;
	double time;
	World() : env(new PxPhysicsEnvironment(false, NULL)), time(0.0) {}
	~World()
	{
		for (PxPhysicsController *c : ctrls) {
			delete c;
		}
		delete env;
	}
	void step(int frames = 1)
	{
		for (int i = 0; i < frames; i++) {
			time += 1.0 / 60.0;
			env->ProceedDeltaTime(time, 1.0f / 60.0f, 1.0f / 60.0f);
		}
	}
};

enum Kind { STATIC, DYNAMIC, RIGID, SENSOR, GHOST, CHARACTER };

static PxShapeDesc *shape_box(float hx, float hy, float hz)
{
	PxShapeDesc *s = new PxShapeDesc();
	s->m_type = PHY_SHAPE_BOX;
	s->m_halfExtents = physx::PxVec3(hx, hy, hz);
	return s;
}

static PxShapeDesc *shape_sphere(float r)
{
	PxShapeDesc *s = new PxShapeDesc();
	s->m_type = PHY_SHAPE_SPHERE;
	s->m_radius = r;
	return s;
}

static PxShapeDesc *shape_capsule(float r, float h)
{
	PxShapeDesc *s = new PxShapeDesc();
	s->m_type = PHY_SHAPE_CAPSULE;
	s->m_radius = r;
	s->m_height = h;
	return s;
}

static PxPhysicsController *add(World &w, PxShapeDesc *shape, Kind kind, float x, float y, float z,
                                float mass = 1.0f, float friction = 0.5f, float restitution = 0.0f,
                                std::function<void(PxCtrlInfo &)> tweak = nullptr)
{
	PxCtrlInfo ci;
	ci.shape = shape;
	ci.mass = mass;
	ci.friction = friction;
	ci.restitution = restitution;
	switch (kind) {
		case STATIC:
			ci.filterGroup = PXF_STATIC;
			ci.filterMask = PXF_ALL ^ PXF_STATIC;
			break;
		case DYNAMIC:
		case RIGID:
			ci.dyna = true;
			ci.rigid = kind == RIGID;
			ci.filterGroup = PXF_DEFAULT;
			ci.filterMask = PXF_ALL;
			break;
		case SENSOR:
			ci.sensor = true;
			ci.noResponse = true;
			ci.filterGroup = PXF_SENSOR;
			ci.filterMask = PXF_ALL ^ PXF_SENSOR;
			break;
		case GHOST:
			ci.noResponse = true;
			ci.filterGroup = PXF_STATIC;
			ci.filterMask = PXF_ALL ^ PXF_STATIC;
			break;
		case CHARACTER:
			ci.character = true;
			ci.noResponse = true;
			ci.filterGroup = PXF_CHARACTER;
			ci.filterMask = PXF_ALL;
			break;
	}
	if (tweak) {
		tweak(ci);
	}
	PxDefaultMotionState *ms = new PxDefaultMotionState();
	ms->SetWorldPosition(x, y, z);
	PxPhysicsController *c = w.env->CreateController(ci, ms, true);
	shape->Release();
	w.ctrls.push_back(c);
	return c;
}

static MT_Vector3 pos(PxPhysicsController *c)
{
	float x, y, z;
	c->GetMotionState()->GetWorldPosition(x, y, z);
	return MT_Vector3(x, y, z);
}

static PxPhysicsController *ground(World &w, float friction = 0.5f, float restitution = 0.0f)
{
	return add(w, shape_box(50, 50, 0.5f), STATIC, 0, 0, -0.5f, 0.0f, friction, restitution);
}

/* ------------------------------------------------------------------------- */

static void test_fall()
{
	printf("fall: a sphere dropped from 10 m for 1 s (free fall, no air)\n");
	World w;
	PxPhysicsController *s = add(w, shape_sphere(0.5f), RIGID, 0, 0, 10);
	w.step(60);
	const double expect = 10.0 - 0.5 * 9.81 * 1.0;
	CHECK(fabs(pos(s).z() - expect) < 0.12, "z after 1 s = %.3f (expected %.3f)", pos(s).z(), expect);
	CHECK(fabs(s->GetLinearVelocity().z() + 9.81) < 0.2, "vz = %.3f (expected -9.81)", s->GetLinearVelocity().z());
}

static void test_stack()
{
	printf("stack: 8 boxes of 1 m on the ground, 6 s\n");
	World w;
	ground(w);
	std::vector<PxPhysicsController *> boxes;
	for (int i = 0; i < 8; i++) {
		boxes.push_back(add(w, shape_box(0.5f, 0.5f, 0.5f), RIGID, 0, 0, 0.5f + i * 1.0f + 0.001f * i));
	}
	w.step(360);
	double maxdx = 0, maxdz = 0;
	for (int i = 0; i < 8; i++) {
		const MT_Vector3 p = pos(boxes[i]);
		maxdx = std::max(maxdx, (double)sqrt(p.x() * p.x() + p.y() * p.y()));
		maxdz = std::max(maxdz, fabs(p.z() - (0.5 + i)));
	}
	CHECK(maxdx < 0.02, "sideways drift %.4f m", maxdx);
	CHECK(maxdz < 0.03, "height error %.4f m", maxdz);
	CHECK(boxes[7]->GetLinearVelocity().length() < 0.05, "top box at rest (%.4f m/s)",
	      boxes[7]->GetLinearVelocity().length());
}

static void test_bounce()
{
	printf("bounce: a sphere (bounce 0.8) dropped 5 m on a floor (bounce 1.0)\n");
	World w;
	ground(w, 0.5f, 1.0f);
	PxPhysicsController *s = add(w, shape_sphere(0.5f), RIGID, 0, 0, 5.5f, 1.0f, 0.5f, 0.8f);
	/* first impact, then the top of the first rebound */
	double apex = 0;
	bool hit = false;
	for (int i = 0; i < 240; i++) {
		w.step();
		const double vz = s->GetLinearVelocity().z();
		if (!hit && vz > 0) {
			hit = true;
		}
		if (hit) {
			apex = std::max(apex, (double)pos(s).z() - 0.5);
			if (vz < 0 && apex > 0.5) {
				break;
			}
		}
	}
	/* bounce multiplies: 0.8 x 1.0, the rebound height 0.8^2 x 5 m */
	const double expect = 0.64 * 5.0;
	CHECK(fabs(apex - expect) < 0.4, "rebound %.2f m (expected about %.2f)", apex, expect);
}

static void test_friction()
{
	printf("friction: a box (friction 0.5) slides at 5 m/s on a floor (friction 0.5)\n");
	World w;
	ground(w, 0.5f);
	PxPhysicsController *b = add(w, shape_box(0.5f, 0.5f, 0.5f), RIGID, 0, 0, 0.5f, 1.0f, 0.5f);
	w.step(10);
	b->SetLinearVelocity(MT_Vector3(5, 0, 0), false);
	const double x0 = pos(b).x();
	w.step(240);
	/* friction multiplies: 0.25; stop distance v^2 / (2 mu g) */
	const double expect = 25.0 / (2 * 0.25 * 9.81);
	const double d = pos(b).x() - x0;
	CHECK(fabs(d - expect) < 0.15 * expect, "slid %.2f m (expected about %.2f)", d, expect);
	CHECK(b->GetLinearVelocity().length() < 0.05, "stopped (%.3f m/s)", b->GetLinearVelocity().length());
}

static void test_damping()
{
	printf("damping: linear damping 0.5, no gravity, 10 m/s for 1 s\n");
	World w;
	w.env->SetGravity(0, 0, 0);
	PxPhysicsController *s = add(w, shape_sphere(0.5f), RIGID, 0, 0, 0, 1.0f, 0.5f, 0.0f,
	                             [](PxCtrlInfo &ci) { ci.linearDamping = 0.5f; ci.angularDamping = 0.5f; });
	s->SetLinearVelocity(MT_Vector3(10, 0, 0), false);
	s->SetAngularVelocity(MT_Vector3(0, 0, 4), false);
	w.step(60);
	/* Bullet: (1 - d) per second */
	CHECK(fabs(s->GetLinearVelocity().x() - 5.0) < 0.15, "speed %.3f (expected 5.0)", s->GetLinearVelocity().x());
	CHECK(fabs(s->GetAngularVelocity().z() - 2.0) < 0.1, "spin %.3f (expected 2.0)", s->GetAngularVelocity().z());
}

static void test_dynamic_no_turn()
{
	printf("dynamic: a Dynamic (not Rigid Body) box hit off-centre does not turn\n");
	World w;
	w.env->SetGravity(0, 0, 0);
	PxPhysicsController *b = add(w, shape_box(0.5f, 0.5f, 0.5f), DYNAMIC, 0, 0, 0);
	PxPhysicsController *r = add(w, shape_box(0.5f, 0.5f, 0.5f), RIGID, 3, 0, 0);
	b->ApplyImpulse(MT_Point3(0, 0.5, 0), MT_Vector3(1, 0, 0), false);
	r->ApplyImpulse(MT_Point3(3, 0.5, 0), MT_Vector3(1, 0, 0), false);
	w.step(30);
	CHECK(b->GetAngularVelocity().length() < 1e-4, "dynamic spin %.5f", b->GetAngularVelocity().length());
	CHECK(r->GetAngularVelocity().length() > 0.5, "rigid body spin %.3f", r->GetAngularVelocity().length());
	CHECK(fabs(b->GetLinearVelocity().x() - 1.0) < 0.01, "dynamic speed %.3f (expected 1.0)",
	      b->GetLinearVelocity().x());
}

static void test_kinematic_static()
{
	printf("kinematic: a static platform moved by the game carries a box\n");
	World w;
	PxPhysicsController *p = add(w, shape_box(2, 2, 0.25f), STATIC, 0, 0, 0);
	PxPhysicsController *b = add(w, shape_box(0.25f, 0.25f, 0.25f), RIGID, 0, 0, 0.5f, 1.0f, 1.0f);
	w.step(30);
	/* the game moves the platform up 1 cm per frame for 1 s */
	for (int i = 0; i < 60; i++) {
		MT_Vector3 q = pos(p);
		p->GetMotionState()->SetWorldPosition(q.x(), q.y(), q.z() + 0.01f);
		p->SetTransform();
		w.step();
	}
	CHECK(fabs(pos(p).z() - 0.6) < 1e-3, "platform at %.3f (expected 0.600)", pos(p).z());
	CHECK(fabs(pos(b).z() - 1.1) < 0.05, "box rode up to %.3f (expected about 1.10)", pos(b).z());
}

static void test_filter()
{
	printf("filter: objects whose collision masks exclude each other pass through\n");
	World w;
	ground(w);
	auto only_static = [](PxCtrlInfo &ci) { ci.filterMask = PXF_STATIC; };
	PxPhysicsController *lo = add(w, shape_box(0.5f, 0.5f, 0.5f), RIGID, 0, 0, 0.5f, 1, 0.5f, 0, only_static);
	PxPhysicsController *hi = add(w, shape_box(0.5f, 0.5f, 0.5f), RIGID, 0, 0, 3.0f, 1, 0.5f, 0, only_static);
	w.step(120);
	CHECK(fabs(pos(hi).z() - 0.5) < 0.05, "upper box went through to %.3f", pos(hi).z());
	(void)lo;
}

/* ------------------------------------------------------------------------- */

struct Reports {
	int calls = 0;
	std::vector<std::pair<void *, void *>> pairs;
	PHY_CollData last;
};

static bool on_touch(void *user, void *a, void *b, const PHY_CollData *data)
{
	Reports *r = (Reports *)user;
	r->calls++;
	r->pairs.push_back(std::make_pair(a, b));
	if (data) {
		r->last = *data;
	}
	return false;
}

static int g_broadphase = 0;
static bool on_broadphase(void *, void *, void *, const PHY_CollData *)
{
	g_broadphase++;
	return true;
}

static void test_collision_reports()
{
	printf("collision: a touch sensor on a ball resting on the ground, then the ball removed\n");
	World w;
	Reports r;
	w.env->AddTouchCallback(PHY_OBJECT_RESPONSE, on_touch, &r);
	PxPhysicsController *g = ground(w);
	PxPhysicsController *s = add(w, shape_sphere(0.5f), RIGID, 0, 0, 2.0f);
	w.env->RequestCollisionCallback(s);
	w.step(60);
	r.calls = 0;
	r.pairs.clear();
	w.step(120);
	/* reported every frame while touching, also when the ball sleeps */
	CHECK(r.calls >= 118 && r.calls <= 122, "reports over 120 frames: %d", r.calls);
	bool right = !r.pairs.empty();
	for (auto &p : r.pairs) {
		right = right && ((p.first == s && p.second == g) || (p.first == g && p.second == s));
	}
	CHECK(right, "the pair is the ball and the ground");
	CHECK(r.last.m_normal.z() > 0.9, "contact normal from the ground to the ball (%.2f %.2f %.2f)", r.last.m_normal.x(),
	      r.last.m_normal.y(), r.last.m_normal.z());
	/* point1 is on the ball, at its bottom */
	CHECK(fabs(r.last.m_point1.z()) < 0.05, "contact point z %.3f", r.last.m_point1.z());
	w.env->RemoveCollisionCallback(s);
	r.calls = 0;
	w.step(10);
	CHECK(r.calls == 0, "no reports after the sensor goes: %d", r.calls);
	w.env->RequestCollisionCallback(s);
	s->SetLinearVelocity(MT_Vector3(0, 0, 8), false);
	w.step(20);
	r.calls = 0;
	w.step(5);
	CHECK(r.calls == 0, "no reports while the ball is in the air: %d", r.calls);
}

static void test_near_sensor()
{
	printf("near: a sphere sensor (radius 2) at the origin, a box flying through\n");
	World w;
	w.env->SetGravity(0, 0, 0);
	Reports r;
	w.env->AddTouchCallback(PHY_OBJECT_RESPONSE, on_touch, &r);
	w.env->AddTouchCallback(PHY_BROADPH_RESPONSE, on_broadphase, NULL);
	PxPhysicsController *sensor = (PxPhysicsController *)w.env->CreateSphereController(2.0f, MT_Vector3(0, 0, 0));
	w.ctrls.push_back(sensor);
	w.env->AddSensor(sensor);
	w.env->RequestCollisionCallback(sensor);
	PxPhysicsController *b = add(w, shape_box(0.25f, 0.25f, 0.25f), RIGID, -6, 0, 0);
	b->SetLinearVelocity(MT_Vector3(6, 0, 0), false);
	int inside = 0, first = -1, last = -1;
	for (int i = 0; i < 120; i++) {
		r.calls = 0;
		w.step();
		if (r.calls) {
			inside++;
			if (first < 0) {
				first = i;
			}
			last = i;
		}
	}
	/* inside from x = -2.25 to 2.25: 4.5 m at 6 m/s = 45 frames */
	CHECK(abs(inside - 45) <= 3, "reported in %d frames (expected about 45)", inside);
	CHECK(abs(first - 37) <= 3, "first at frame %d (expected about 37)", first);
	CHECK(g_broadphase == 1, "property filter asked once: %d", g_broadphase);
	CHECK(fabs(b->GetLinearVelocity().x() - 6.0) < 1e-3, "the box went through untouched (%.3f m/s)",
	      b->GetLinearVelocity().x());
	(void)last;
}

/* ------------------------------------------------------------------------- */

struct RayCb : public PHY_IRayCastFilterCallback {
	PHY_RayCastResult hit;
	bool got = false;
	PHY_IPhysicsController *skip = NULL;
	RayCb(PHY_IPhysicsController *ignore, bool faceNormal = false, bool faceUV = false)
	    : PHY_IRayCastFilterCallback(ignore, faceNormal, faceUV)
	{
		memset(&hit, 0, sizeof(hit));
	}
	bool needBroadphaseRayCast(PHY_IPhysicsController *c) override { return c != skip; }
	void reportHit(PHY_RayCastResult *r) override
	{
		hit = *r;
		got = true;
	}
};

static void test_rays()
{
	printf("rays: down onto the ground, past a sensor, onto a UV-mapped triangle mesh\n");
	World w;
	PxPhysicsController *g = ground(w);
	PxPhysicsController *sensor = (PxPhysicsController *)w.env->CreateSphereController(1.0f, MT_Vector3(0, 0, 3));
	w.ctrls.push_back(sensor);
	w.env->AddSensor(sensor);
	PxPhysicsController *box = add(w, shape_box(0.5f, 0.5f, 0.5f), STATIC, 5, 0, 0.5f);
	{
		RayCb cb(NULL);
		PHY_IPhysicsController *hit = w.env->RayTest(cb, 0, 0, 10, 0, 0, -10);
		CHECK(hit == g, "hit the ground, not the sensor");
		CHECK(fabs(cb.hit.m_hitPoint.z()) < 1e-3, "hit point z %.4f", cb.hit.m_hitPoint.z());
		CHECK(fabs(cb.hit.m_hitNormal.z() - 1.0) < 1e-3, "normal up (%.3f)", cb.hit.m_hitNormal.z());
	}
	{
		RayCb cb(box);
		PHY_IPhysicsController *hit = w.env->RayTest(cb, 5, 0, 10, 5, 0, -10);
		CHECK(hit == g, "ignore controller skipped (hit the ground)");
	}
	{
		RayCb cb(NULL);
		PHY_IPhysicsController *hit = w.env->RayTest(cb, 0, 0, 10, 0, 0, 5);
		CHECK(hit == NULL, "short ray hits nothing");
	}

	/* a 2 x 2 quad split in two triangles, polygon 7, UV 0..1 */
	CcdShapeConstructionInfo *mesh = new CcdShapeConstructionInfo();
	const float v[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
	for (float f : v) {
		mesh->m_vertexArray.push_back(f);
	}
	mesh->m_triFaceArray = {0, 1, 2, 0, 2, 3};
	mesh->m_polygonIndexArray = {7, 7};
	CcdShapeConstructionInfo::UVco uv[6] = {{{0, 0}}, {{1, 0}}, {{1, 1}}, {{0, 0}}, {{1, 1}}, {{0, 1}}};
	mesh->m_triFaceUVcoArray.assign(uv, uv + 6);
	PxShapeDesc *ms = new PxShapeDesc();
	ms->m_type = PHY_SHAPE_MESH;
	ms->m_mesh = mesh;
	PxPhysicsController *quad = add(w, ms, STATIC, 0, 20, 2);
	{
		RayCb cb(NULL, true, true);
		PHY_IPhysicsController *hit = w.env->RayTest(cb, 0.5f, 20 - 0.5f, 5, 0.5f, 20 - 0.5f, -5);
		CHECK(hit == quad, "hit the mesh");
		CHECK(cb.hit.m_polygon == 7, "polygon %d (expected 7)", cb.hit.m_polygon);
		CHECK(cb.hit.m_hitUVOK && fabs(cb.hit.m_hitUV[0] - 0.75) < 1e-3 && fabs(cb.hit.m_hitUV[1] - 0.25) < 1e-3,
		      "UV (%.3f, %.3f) (expected 0.75, 0.25)", cb.hit.m_hitUV[0], cb.hit.m_hitUV[1]);
		CHECK(fabs(cb.hit.m_hitNormal.z() - 1.0) < 1e-3, "face normal up");
		CHECK(fabs(cb.hit.m_hitPoint.z() - 2.0) < 1e-3, "hit z %.4f", cb.hit.m_hitPoint.z());
	}
	{
		/* from below: the face normal stays the face's (down the ray: up) */
		RayCb cb(NULL, true, false);
		w.env->RayTest(cb, -0.5f, 20.5f, 1.0f, -0.5f, 20.5f, 5);
		CHECK(cb.got && fabs(cb.hit.m_hitNormal.z() - 1.0) < 1e-3, "from below, face normal (%.2f)",
		      cb.hit.m_hitNormal.z());
	}
	/* a dynamic box falls on the mesh and rests on it */
	PxPhysicsController *b = add(w, shape_box(0.25f, 0.25f, 0.25f), RIGID, 0, 20, 4);
	w.step(120);
	CHECK(fabs(pos(b).z() - 2.25) < 0.03, "box rests on the mesh at %.3f (expected 2.25)", pos(b).z());

	/* Bullet: a triangle mesh's collision margin keeps things that far off */
	mesh->AddRef();
	PxShapeDesc *ms2 = new PxShapeDesc();
	ms2->m_type = PHY_SHAPE_MESH;
	ms2->m_mesh = mesh;
	ms2->m_margin = 0.04f;
	add(w, ms2, STATIC, 0, 30, 2);
	PxPhysicsController *b2 = add(w, shape_box(0.25f, 0.25f, 0.25f), RIGID, 0, 30, 4);
	w.step(120);
	CHECK(fabs(pos(b2).z() - 2.29) < 0.01, "with a margin of 4 cm it rests at %.3f (expected 2.29)", pos(b2).z());
}

/* ------------------------------------------------------------------------- */

static void test_joints()
{
	printf("joints: hinge pendulum on the world, ball joint chain, 6DOF to a static rail\n");
	World w;
	/* hinge about X with the pivot 1 m above the sphere, a push along Y */
	PxPhysicsController *s = add(w, shape_sphere(0.1f), RIGID, 0, 0, 5);
	int hinge = w.env->CreateConstraint(s, NULL, PHY_LINEHINGE_CONSTRAINT, 0, 0, 1, 1, 0, 0);
	CHECK(hinge > 0, "hinge id %d", hinge);
	/* a ball joint chain: a second sphere 1 m below */
	PxPhysicsController *s2 = add(w, shape_sphere(0.1f), RIGID, 0, 0, 4);
	int ball = w.env->CreateConstraint(s, s2, PHY_POINT2POINT_CONSTRAINT, 0, 0, -0.5f, 0, 0, 0);
	CHECK(ball > 0, "ball joint id %d", ball);
	s->SetLinearVelocity(MT_Vector3(0, 2, 0), false);
	double maxR = 0, minR = 10, maxX = 0, maxY = 0, maxD = 0;
	for (int i = 0; i < 240; i++) {
		w.step();
		const MT_Vector3 p = pos(s);
		const double r = (p - MT_Vector3(0, 0, 6)).length();
		maxR = std::max(maxR, (double)r);
		minR = std::min(minR, (double)r);
		maxX = std::max(maxX, (double)fabs(p.x()));
		maxY = std::max(maxY, (double)fabs(p.y()));
		/* the ball joint's pivot: 0.5 m below s in s's frame */
		maxD = std::max(maxD, (double)(pos(s2) - p).length());
	}
	CHECK(maxR - 1.0 < 0.01 && 1.0 - minR < 0.01, "hinge radius %.4f .. %.4f (expected 1)", minR, maxR);
	CHECK(maxX < 0.01, "stays in the hinge's plane (x %.4f)", maxX);
	CHECK(maxY > 0.2, "it swings (y up to %.2f)", maxY);
	CHECK(maxD < 1.01, "chain link length up to %.4f (pivots 0.5 + 0.5)", maxD);

	/* 6DOF to a static rail: locked at first, then Z free */
	PxPhysicsController *rail = add(w, shape_box(2, 0.05f, 0.05f), STATIC, 10, 0, 3);
	PxPhysicsController *ring = add(w, shape_box(0.2f, 0.05f, 0.2f), RIGID, 10, 0, 2.5f);
	int d6 = w.env->CreateConstraint(ring, rail, PHY_GENERIC_6DOF_CONSTRAINT, 0, 0, 0.5f, 0, 0, 0);
	CHECK(d6 > 0, "6DOF id %d", d6);
	w.step(60);
	CHECK(fabs(pos(ring).z() - 2.5) < 0.01, "locked: hangs at %.4f", pos(ring).z());
	/* X free from -1 to 1 (a ring sliding on the rail), pushed */
	w.env->SetConstraintParam(d6, 0, -1.0f, 1.0f);
	ring->SetLinearVelocity(MT_Vector3(3, 0, 0), false);
	w.step(120);
	CHECK(fabs(pos(ring).x() - 11.0) < 0.05, "slides to the limit at x %.3f (expected 11)", pos(ring).x());
	CHECK(fabs(pos(ring).z() - 2.5) < 0.01, "still at its height %.4f", pos(ring).z());
	/* rotation about X with a motor: 2 rad/s */
	w.env->SetConstraintParam(d6, 3, 1.0f, -1.0f);
	w.env->SetConstraintParam(d6, 9, 2.0f, 50.0f);
	double avg = 0;
	for (int i = 0; i < 120; i++) {
		w.step();
		if (i >= 60) {
			avg += ring->GetAngularVelocity().x() / 60.0;
		}
		if (getenv("HX_DEBUG") && i % 10 == 0) {
			MT_Vector3 av = ring->GetAngularVelocity();
			printf("    motor frame %d: w (%.3f %.3f %.3f) z %.3f\n", i, av.x(), av.y(), av.z(), pos(ring).z());
		}
	}
	/* the ring turns about the rail (the pivot), lifting itself: the motor
	 * sags under the load and catches up on the way down, as Bullet's */
	CHECK(fabs(avg - 2.0) < 0.4, "motor spins at %.3f rad/s on average (target 2)", avg);
	CHECK(fabs(w.env->GetConstraintParam(d6, 0) + 1.0) < 0.06, "param 0 reads the X offset %.3f (Bullet: B - A = -1)",
	      w.env->GetConstraintParam(d6, 0));
	/* removing the joint lets the ring fall */
	w.env->RemoveConstraintById(d6);
	for (int i = 0; i < 30; i++) {
		w.step();
		if (getenv("HX_DEBUG") && i % 5 == 0) {
			printf("    free frame %d: z %.3f vz %.3f\n", i, pos(ring).z(), ring->GetLinearVelocity().z());
		}
	}
	CHECK(ring->GetLinearVelocity().z() < -3.5, "falls after the joint goes (vz %.3f)", ring->GetLinearVelocity().z());

	/* cone twist and angular, between two bodies */
	PxPhysicsController *a = add(w, shape_box(0.2f, 0.2f, 0.2f), RIGID, 20, 0, 5, 1, 0.5f, 0,
	                             [](PxCtrlInfo &ci) { ci.linearFactor = physx::PxVec3(0.0f); ci.angularFactor = physx::PxVec3(0.0f); });
	PxPhysicsController *c = add(w, shape_box(0.2f, 0.2f, 0.2f), RIGID, 20, 0, 4);
	int cone = w.env->CreateConstraint(a, c, PHY_CONE_TWIST_CONSTRAINT, 0, 0, -0.5f, 0, 0, -1);
	CHECK(cone > 0, "cone twist id %d", cone);
	w.env->SetConstraintParam(cone, 4, 0.0f, 0.3f);
	w.env->SetConstraintParam(cone, 5, 0.0f, 0.3f);
	c->SetLinearVelocity(MT_Vector3(0, 5, 0), false);
	double maxAng = 0;
	for (int i = 0; i < 120; i++) {
		w.step();
		const MT_Vector3 d = pos(c) - pos(a);
		maxAng = std::max(maxAng, (double)acos(std::min(1.0, std::max(-1.0, (double)(-d.z() / d.length())))));
	}
	CHECK(maxAng < 0.3 + 0.06, "cone swing up to %.3f rad (limit 0.3)", maxAng);
	CHECK(maxAng > 0.2, "it swings to the limit (%.3f)", maxAng);
	CHECK(w.env->GetAppliedImpulse(cone) > 0.0f, "applied impulse %.4f", w.env->GetAppliedImpulse(cone));
}

static void test_joint_scale()
{
	printf("joint scale: a 6DOF joint of an object scaled 0.4 (Bullet: its limits are in scaled units)\n");
	World w;
	PxPhysicsController *rail = add(w, shape_box(0.05f, 0.05f, 0.05f), STATIC, 0, 0, 1);
	PxPhysicsController *b = add(w, shape_box(0.2f, 0.2f, 0.2f), RIGID, 0, 0, 1);
	/* the axes as SetupObjectConstraints passes them: the frame times the scale */
	int id = w.env->CreateConstraint(b, rail, PHY_GENERIC_6DOF_CONSTRAINT, 0, 0, 0, 0.4f, 0, 0, 0, 0.4f, 0, 0, 0, 0.4f);
	w.env->SetConstraintParam(id, 0, -1.0f, 1.0f);
	for (int i = 1; i < 6; i++) {
		w.env->SetConstraintParam(id, i, 0.0f, 0.0f);
	}
	b->SetLinearVelocity(MT_Vector3(3, 0, 0), false);
	w.step(60);
	CHECK(fabs(pos(b).x() - 0.4) < 0.03, "stops at x %.3f (limit 1 x scale 0.4)", pos(b).x());
	CHECK(fabs(w.env->GetConstraintParam(id, 0) + 1.0) < 0.08, "param 0 reads %.3f in scaled units",
	      w.env->GetConstraintParam(id, 0));
}

/* ------------------------------------------------------------------------- */

static void test_character()
{
	printf("character: walks 0.1 m per tick, jumps, steps over a 0.2 m kerb\n");
	World w;
	ground(w);
	PxPhysicsController *c = add(w, shape_capsule(0.4f, 1.0f), CHARACTER, 0, 0, 1.0f, 1, 0.5f, 0,
	                             [](PxCtrlInfo &ci) { ci.stepHeight = 0.3f; ci.jumpSpeed = 10.0f; ci.fallSpeed = 55.0f; });
	PHY_ICharacter *ch = (PHY_ICharacter *)c->GetCharacter();
	CHECK(ch != NULL, "has a character");
	if (!ch) {
		return;
	}
	w.step(60);
	CHECK(ch->OnGround(), "on the ground after 1 s");
	const double z0 = pos(c).z();
	CHECK(fabs(z0 - 0.9) < 0.05, "stands at %.3f (half height 0.9)", z0);
	ch->SetWalkDirection(MT_Vector3(0.1, 0, 0));
	w.step(60);
	ch->SetWalkDirection(MT_Vector3(0, 0, 0));
	CHECK(fabs(pos(c).x() - 6.0) < 0.1, "walked %.3f m in 60 ticks (expected 6)", pos(c).x());
	ch->Jump();
	double top = 0;
	for (int i = 0; i < 90; i++) {
		w.step();
		top = std::max(top, (double)(pos(c).z() - z0));
	}
	/* v^2 / 2g with g = 29.4 */
	CHECK(fabs(top - 100.0 / (2 * 29.4)) < 0.15, "jumped %.3f m (expected %.3f)", top, 100.0 / (2 * 29.4));
	CHECK(ch->OnGround(), "landed");
	/* a kerb 0.2 m high at x = 8 */
	add(w, shape_box(1, 5, 0.1f), STATIC, 9, 0, 0.1f);
	ch->SetWalkDirection(MT_Vector3(0.05, 0, 0));
	w.step(60);
	ch->SetWalkDirection(MT_Vector3(0, 0, 0));
	w.step(30);
	CHECK(pos(c).x() > 8.5 && fabs(pos(c).z() - (z0 + 0.2)) < 0.06, "on the kerb at x %.2f z %.3f", pos(c).x(),
	      pos(c).z());
	/* a ray from above sees the character */
	RayCb cb(NULL);
	CHECK(w.env->RayTest(cb, pos(c).x(), 0, 10, pos(c).x(), 0, -10) == c, "rays hit the character");
}

/* ------------------------------------------------------------------------- */

static void test_vehicle()
{
	printf("vehicle: an 800 kg car on 4 raycast wheels, rear-wheel drive, then brakes\n");
	World w;
	ground(w, 0.8f);
	PxPhysicsController *car = add(w, shape_box(1.0f, 2.0f, 0.4f), RIGID, 0, 0, 1.2f, 800.0f);
	int id = w.env->CreateConstraint(car, NULL, PHY_VEHICLE_CONSTRAINT, 0, 0, 0, 0, 0, 0);
	PHY_IVehicle *v = w.env->GetVehicleConstraint(id);
	CHECK(v != NULL, "vehicle id %d", id);
	if (!v) {
		return;
	}
	v->SetCoordinateSystem(0, 2, 1);
	std::vector<PxDefaultMotionState *> wheels;
	const float wx = 0.9f, wy = 1.4f, wz = -0.2f;
	const float pts[4][2] = {{-wx, wy}, {wx, wy}, {-wx, -wy}, {wx, -wy}};
	for (int i = 0; i < 4; i++) {
		PxDefaultMotionState *ms = new PxDefaultMotionState();
		wheels.push_back(ms);
		v->AddWheel(ms, MT_Vector3(pts[i][0], pts[i][1], wz), MT_Vector3(0, 0, -1), MT_Vector3(-1, 0, 0), 0.4f,
		            0.4f, i < 2);
		v->SetSuspensionStiffness(20.0f, i);
		v->SetSuspensionDamping(2.3f, i);
		v->SetSuspensionCompression(4.4f, i);
		v->SetWheelFriction(10.0f, i);
		v->SetRollInfluence(0.1f, i);
	}
	CHECK(v->GetNumWheels() == 4, "4 wheels");
	w.step(120);
	/* rests: the suspension holds the car (compression g / (4 k) per unit mass) */
	const double rest = pos(car).z();
	/* fully out 1.0; Bullet: compression g / (4 x 20) = 0.123 */
	CHECK(fabs(rest - 0.877) < 0.02, "rests at %.3f (expected 0.877)", rest);
	CHECK(car->GetLinearVelocity().length() < 0.05, "settled (%.3f m/s)", car->GetLinearVelocity().length());
	float wxp, wyp, wzp;
	v->GetWheelPosition(0, wxp, wyp, wzp);
	CHECK(fabs(wzp - 0.4) < 0.05, "front wheel centre at %.3f (radius 0.4)", wzp);
	/* drive */
	for (int i = 2; i < 4; i++) {
		v->ApplyEngineForce(-1500.0f, i);  /* BGE: forward is a negative force with axle -X */
	}
	w.step(180);
	const MT_Vector3 p = pos(car);
	CHECK(p.y() > 5.0, "drove forward %.2f m", p.y());
	CHECK(fabs(p.x()) < 0.3, "straight (x %.3f)", p.x());
	CHECK(fabs(p.z() - rest) < 0.1, "upright, height %.3f", p.z());
	CHECK(v->GetWheelRotation(2) != 0.0f, "wheels turn (%.2f rad)", v->GetWheelRotation(2));
	/* steer */
	for (int i = 0; i < 2; i++) {
		v->SetSteeringValue(0.3f, i);
	}
	const double x0 = p.x();
	w.step(60);
	CHECK(fabs(pos(car).x() - x0) > 0.5, "steering turns the car (x moved %.2f)", pos(car).x() - x0);
	/* brake */
	for (int i = 0; i < 4; i++) {
		v->ApplyEngineForce(0.0f, i);
		v->ApplyBraking(40.0f, i);
		v->SetSteeringValue(0.0f, i);
	}
	w.step(240);
	CHECK(car->GetLinearVelocity().length() < 0.3, "braked to %.3f m/s", car->GetLinearVelocity().length());
}

/* ------------------------------------------------------------------------- */

static void test_suspend_replica()
{
	printf("objects: suspend and restore dynamics, a replica, removal, compound\n");
	World w;
	ground(w);
	PxPhysicsController *b = add(w, shape_box(0.5f, 0.5f, 0.5f), RIGID, 0, 0, 3);
	b->SuspendDynamics(false);
	w.step(60);
	CHECK(fabs(pos(b).z() - 3.0) < 1e-4, "suspended: stays at %.3f", pos(b).z());
	b->RestoreDynamics();
	w.step(90);
	CHECK(fabs(pos(b).z() - 0.5) < 0.02, "restored: fell to %.3f", pos(b).z());

	/* a replica (Add Object) */
	PxPhysicsController *r = (PxPhysicsController *)b->GetReplica();
	PxDefaultMotionState *ms = new PxDefaultMotionState();
	ms->SetWorldPosition(3, 0, 4);
	r->PostProcessReplica(ms, NULL);
	w.env->AddController(r);
	w.ctrls.push_back(r);
	w.step(90);
	CHECK(fabs(pos(r).z() - 0.5) < 0.02 && fabs(pos(r).x() - 3.0) < 0.02, "replica fell at its place (%.2f, %.2f)",
	      pos(r).x(), pos(r).z());
	/* removed from the scene: nothing touches it */
	w.env->RemoveController(r);
	RayCb cb(NULL);
	CHECK(w.env->RayTest(cb, 3, 0, 10, 3, 0, -10) != r, "removed objects are not hit by rays");

	/* a compound: a child box added to a parent at run time */
	PxShapeDesc *comp = new PxShapeDesc();
	comp->m_type = PHY_SHAPE_COMPOUND;
	{
		PxShapeDesc::Child ch;
		ch.desc = shape_box(0.5f, 0.5f, 0.5f);
		ch.local = physx::PxTransform(physx::PxIdentity);
		ch.scale = physx::PxVec3(1.0f);
		ch.owner = NULL;
		comp->m_children.push_back(ch);
	}
	PxPhysicsController *parent = add(w, comp, RIGID, 10, 0, 0.5f);
	PxPhysicsController *child = add(w, shape_box(0.5f, 0.5f, 0.5f), STATIC, 11, 0, 0.5f);
	w.step(30);
	parent->AddCompoundChild(child);
	CHECK(parent->IsCompound(), "parent is compound");
	RayCb cb2(NULL);
	CHECK(w.env->RayTest(cb2, 11, 0, 10, 11, 0, -10) == parent, "ray on the child's place hits the parent");
	parent->RemoveCompoundChild(child);
	RayCb cb3(NULL);
	CHECK(w.env->RayTest(cb3, 11, 0, 10, 11, 0, -10) != parent, "after removal it does not");
}

static void test_substeps()
{
	printf("timing: 2 sub-steps, a 30 fps frame, a long frame (at most 25 steps)\n");
	World w;
	w.env->SetNumTimeSubSteps(2);
	add(w, shape_sphere(0.5f), RIGID, 0, 0, 100);
	const int s0 = w.env->GetStats().steps;
	w.time += 1.0 / 60.0;
	w.env->ProceedDeltaTime(w.time, 1.0f / 60.0f, 1.0f / 60.0f);
	CHECK(w.env->GetStats().steps - s0 == 2, "one frame: %d steps (expected 2)", w.env->GetStats().steps - s0);
	const int s1 = w.env->GetStats().steps;
	w.env->ProceedDeltaTime(w.time, 1.0f / 60.0f, 1.0f / 30.0f);
	CHECK(w.env->GetStats().steps - s1 == 4, "30 fps frame: %d steps (expected 4)", w.env->GetStats().steps - s1);
	const int s2 = w.env->GetStats().steps;
	w.env->ProceedDeltaTime(w.time, 1.0f / 60.0f, 2.0f);
	CHECK(w.env->GetStats().steps - s2 == 25, "a 2 s frame: %d steps (expected 25)", w.env->GetStats().steps - s2);
}

static void test_fh()
{
	printf("fh: a Fh spring holds a ball above the ground\n");
	World w;
	add(w, shape_box(50, 50, 0.5f), STATIC, 0, 0, -0.5f, 0.0f, 0.5f, 0.0f, [](PxCtrlInfo &ci) {
		ci.fhSpring = 0.5f;
		ci.fhDamping = 0.3f;
		ci.fhDistance = 1.0f;
	});
	/* Bullet: the spring is the ground material's (Fh distance, force, damping),
	 * the object only turns it on */
	PxPhysicsController *g = w.ctrls.back();
	(void)g;
	PxPhysicsController *b = add(w, shape_sphere(0.5f), DYNAMIC, 0, 0, 2.0f, 1, 0.5f, 0, [](PxCtrlInfo &ci) {
		ci.doFh = true;
		ci.radius = 0.5f;
	});
	w.step(300);
	const double z = pos(b).z();
	CHECK(z > 0.6 && z < 1.5, "floats at %.3f (above its radius 0.5)", z);
	CHECK(fabs(b->GetLinearVelocity().z()) < 0.2, "settled (%.3f m/s)", b->GetLinearVelocity().z());
}

static void test_sleep()
{
	printf("sleep: Blender's default deactivation time 0 (Bullet: never sleeps); a damped chain swings down\n");
	World w;
	w.env->SetDeactivationTime(0.0f);
	PxPhysicsController *prev = add(w, shape_box(0.05f, 0.05f, 0.05f), STATIC, 0, 0, 4, 0, 0.5f, 0,
	                                [](PxCtrlInfo &ci) { ci.noResponse = true; });
	std::vector<PxPhysicsController *> balls;
	for (int k = 0; k < 3; k++) {
		PxPhysicsController *b = add(w, shape_sphere(0.15f), RIGID, 1.0f + k, 0, 4, 1, 0.5f, 0,
		                             [](PxCtrlInfo &ci) { ci.linearDamping = 0.8f; ci.angularDamping = 0.8f; });
		w.env->CreateConstraint(b, prev, PHY_POINT2POINT_CONSTRAINT, -1, 0, 0, 0, 0, 0);
		balls.push_back(b);
		prev = b;
	}
	w.step(600);
	double worst = 0;
	for (int k = 0; k < 3; k++) {
		worst = std::max(worst, (double)(pos(balls[k]) - MT_Vector3(0, 0, 3.0f - k)).length());
	}
	CHECK(worst < 0.05, "the chain hangs straight down after 10 s (worst %.3f m)", worst);
	CHECK(!balls[2]->GetDynamic()->isSleeping(), "nothing sleeps");
	w.env->SetDeactivationTime(2.0f);
	w.step(300);
	CHECK(balls[2]->GetDynamic()->isSleeping(), "with a deactivation time of 2 s it sleeps when still");
}

int main(int argc, char **argv)
{
	const char *only = argc > 1 ? argv[1] : NULL;
	struct T {
		const char *name;
		void (*fn)();
	} tests[] = {
	    {"fall", test_fall},
	    {"stack", test_stack},
	    {"bounce", test_bounce},
	    {"friction", test_friction},
	    {"damping", test_damping},
	    {"dynamic", test_dynamic_no_turn},
	    {"kinematic", test_kinematic_static},
	    {"filter", test_filter},
	    {"collision", test_collision_reports},
	    {"near", test_near_sensor},
	    {"rays", test_rays},
	    {"joints", test_joints},
	    {"jointscale", test_joint_scale},
	    {"character", test_character},
	    {"vehicle", test_vehicle},
	    {"objects", test_suspend_replica},
	    {"timing", test_substeps},
	    {"fh", test_fh},
	    {"sleep", test_sleep},
	};
	for (const T &t : tests) {
		if (only && strcmp(only, t.name)) {
			continue;
		}
		t.fn();
	}
	printf("\n%d passed, %d failed\n", g_pass, g_fail);
	PxShapeDesc::FreeCooked();
	PxSystem::Free();
	return g_fail ? 1 : 0;
}
