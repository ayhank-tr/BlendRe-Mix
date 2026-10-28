/* Native harness: the game engine symbols the PhysX backend links against.
 * The harness never gives controllers a game object, so these are not called
 * (they abort if they are). */
#include <cstdio>
#include <cstdlib>

#include "KX_GameObject.h"
#include "KX_KetsjiEngine.h"
#include "CcdPhysicsController.h"
#include "PxPhysicsEnvironment.h"
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"

static void never(const char *what)
{
	fprintf(stderr, "harness: unexpected call to %s\n", what);
	abort();
}

KX_GameObject *KX_GameObject::GetClientObject(KX_ClientObjectInfo *) { never("GetClientObject"); return NULL; }
std::vector<bRigidBodyJointConstraint *> KX_GameObject::GetConstraints() { never("GetConstraints"); return {}; }
PHY_IPhysicsController *KX_GameObject::GetPhysicsController() { never("GetPhysicsController"); return NULL; }
unsigned short KX_GameObject::GetUserCollisionGroup() { never("GetUserCollisionGroup"); return 0; }
unsigned short KX_GameObject::GetUserCollisionMask() { never("GetUserCollisionMask"); return 0; }
void KX_GameObject::SynchronizeTransformFunc(SG_IObject *, void *, void *) { never("SynchronizeTransformFunc"); }

double g_ticrate = 60.0;
double KX_KetsjiEngine::GetTicRate() { return g_ticrate; }

int g_debugLines = 0;
void KX_RasterizerDrawDebugLine(const MT_Vector3 &, const MT_Vector3 &, const MT_Vector3 &) { g_debugLines++; }

/* the mesh info: the harness fills its arrays itself */
CcdShapeConstructionInfo::~CcdShapeConstructionInfo() {}
bool CcdShapeConstructionInfo::UpdateMesh(KX_GameObject *, RAS_MeshObject *) { never("UpdateMesh"); return false; }

void PxPhysicsEnvironment::ConvertObject(KX_GameObject *, RAS_MeshObject *, DerivedMesh *, KX_Scene *,
                                         PHY_ShapeProps *, PHY_MaterialProps *, PHY_IMotionState *, int, bool, bool)
{
	never("ConvertObject");
}
PHY_ICharacter *PxPhysicsEnvironment::GetCharacterController(KX_GameObject *) { never("GetCharacterController"); return NULL; }
void PxPhysicsEnvironment::SetupObjectConstraints(KX_GameObject *, KX_GameObject *, bRigidBodyJointConstraint *)
{
	never("SetupObjectConstraints");
}

const char *btTypedConstraint::serialize(void *, btSerializer *) const { never("btTypedConstraint::serialize"); return NULL; }
