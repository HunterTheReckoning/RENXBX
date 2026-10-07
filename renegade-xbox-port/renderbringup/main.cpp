/*
** Renegade Xbox port: render bring-up test (3D engine, stage 2, milestone 2, step 1).
**
** Runs the engine's own start-up and frame loop on the Xbox, through the port's Direct3D layer:
**   WW3D::Init               creates Direct3D, enumerates devices, starts the 3D subsystems
**   WW3D::Set_Render_Device  picks color and depth formats, creates the device on pbkit,
**                            builds the missing-texture placeholder (textures and D3DX at work)
**   Begin_Render/End_Render  clears and presents frames through DX8Wrapper, as the game does
**
** Success: after a few status lines, the screen color changes smoothly, with a frame counter.
** If a step fails, the screen says which one.
*/
#include <hal/debug.h>
#include <hal/video.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <SDL.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/xbox.h>

#include "always.h"
#include "ww3d.h"
#include "vector3.h"
#include "wwdebug.h"
#include "assetmgr.h"
#include "dx8wrapper.h"
#include "texture.h"
#include "refcount.h"
#include "xbox_d3d8_stats.h"
#include "ffactory.h"
#include "mixfile.h"
#include "scene.h"
#include "camera.h"
#include "light.h"
#include "rendobj.h"
#include "sphere.h"
#include "hlod.h"
#include "hanim.h"
#include "pscene.h"
#include "wwphys.h"
#include "phys.h"
#include "physlist.h"
#include "saveload.h"
#include "chunkio.h"
#include "definitionmgr.h"
#include "definition.h"
#include "physcoltest.h"
#include "coltype.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8fvf.h"
#include "vertmaterial.h"
#include "shader.h"
#include "matrix3d.h"
#include "matrix4.h"

/* pbkit after the engine headers: it defines _11 ... _44 as macros, which are the member names
** of Direct3D's D3DMATRIX. Nothing here uses those macros, so remove them. */
#include <pbkit/pbkit.h>
#undef _11
#undef _12
#undef _13
#undef _14
#undef _21
#undef _22
#undef _23
#undef _24
#undef _31
#undef _32
#undef _33
#undef _34
#undef _41
#undef _42
#undef _43
#undef _44

/* Step 2 test geometry, drawn through the engine's own DX8Wrapper (engine vertex and index
** buffers, transforms, shader, material), so the whole path from the engine through the port's
** Direct3D layer to the GPU is used.
**
**   cube             a different color at each corner; wound like Renegade's models (front faces
**                    counterclockwise on screen: the engine culls clockwise), so with culling on,
**                    a correct layer shows the outside and never the inside (as the Minigunner must)
**   crossing planes  a red and a blue square crossing at an angle; where they meet must be a
**                    straight line (a curved or ragged one means depth is interpolated wrongly)
**
** Controller: A churn mode (buffers and a texture created and released every frame, to check
** that memory returns), B culling on/off, Y switch scene, X reset the memory baseline,
** Start diagnostic: w = 1 (no perspective correction), to locate depth problems. */
/* --- Test harness ---------------------------------------------------------------------
** One disc, several tests. With no launch data the program shows a menu; choosing a test
** relaunches the program (XLaunchXBEEx) with the test's number in the launch data, so each test
** starts on a freshly started machine with only its own assets loaded. Start + Back together
** relaunches to the menu. */
enum { TEST_MENU, TEST_RENDER, TEST_MODELS, TEST_LEVEL, TEST_ANIMATION, TEST_COUNT };
static const char *const TestNames[TEST_COUNT] = {
	"menu",
	"Render: cube, depth planes, textures",
	"Models: Nod Minigunner, tutorial terrain",
	"Level: M00 tutorial through wwphys",
	"Animation: Havoc, human movement set" };
static int Test = TEST_MENU;
static const DWORD LAUNCH_MAGIC = 0x54584E52;   /* "RNXT" */

/* Relaunch this program (by its own full path, so it works from a disc or a hard drive) with
** the given test; TEST_MENU returns to the menu. Doesn't return on success. */
static void Relaunch(int test)
{
	static char path[260];
	unsigned n = XeImageFileName->Length < sizeof(path) - 1 ? XeImageFileName->Length : sizeof(path) - 1;
	memcpy(path, XeImageFileName->Buffer, n);
	path[n] = '\0';
	static unsigned char data[3072];          /* XLaunchXBEEx copies the whole launch data page */
	memset(data, 0, sizeof(data));
	DWORD words[2] = { LAUNCH_MAGIC, (DWORD)test };
	memcpy(data, words, sizeof(words));
	debugPrint("  relaunching %s for test %d\n", path, test);
	XLaunchXBEEx(path, data);
	debugPrint("  relaunch failed\n");
}

static int Launched_Test(void)
{
	unsigned long type = 0;
	const unsigned char *data = NULL;
	if (XGetLaunchInfo(&type, &data) != 0 || type != LDT_TITLE || !data) return TEST_MENU;
	DWORD words[2];
	memcpy(words, data, sizeof(words));
	if (words[0] != LAUNCH_MAGIC || words[1] >= TEST_COUNT) return TEST_MENU;
	return (int)words[1];
}

static DX8VertexBufferClass *CubeVB, *PlanesVB;
static DX8IndexBufferClass *CubeIB, *PlanesIB;
static VertexMaterialClass *VertexColorMaterial;

/* Step 3: a cube with its own texture coordinates per face, and three textures requested by
** name through the engine's asset manager, as the game loads them. The first name doesn't
** exist, so the engine shows its missing-texture placeholder (uncompressed: tests swizzling). */
static DX8VertexBufferClass *TexCubeVB[3];   /* texture coordinates 0..repeat per face */
static DX8IndexBufferClass *TexCubeIB;
enum { TEXTURE_SCENES = 3 };
static const char *const TextureNames[TEXTURE_SCENES] = { "no_such_texture.dds", "l05_grass.dds", "if_gdi_logo2.dds" };
static const float TextureRepeat[TEXTURE_SCENES] = { 1.0f, 2.0f, 1.0f };   /* grass tiles twice per face */
static TextureClass *Textures[TEXTURE_SCENES];

/* --- The game's file set-up, and a real model -------------------------------------------
** The game looks for files through a chain: loose files first, then Always2.dat (the patch's
** updated files), then Always.dat (Commando/init.cpp builds it with FileFactoryListClass, part
** of the game's own library, not ported yet). This small chain does the same: the first
** factory whose file exists wins; if none has it, the first factory's missing file is returned,
** as the game's does. Each file goes back to the factory that made it. */
class ChainFileFactory : public FileFactoryClass {
public:
	ChainFileFactory() : Count(0) { memset(Out, 0, sizeof(Out)); }
	void Add(FileFactoryClass *factory) { if (Count < 8) List[Count++] = factory; }
	FileClass *Get_File(char const *filename) override
	{
		for (int i = 0; i < Count; i++) {
			FileClass *file = List[i]->Get_File(filename);
			if (file) {
				if (file->Is_Available()) { Remember(file, List[i]); return file; }
				List[i]->Return_File(file);
			}
		}
		FileClass *file = Count ? List[0]->Get_File(filename) : NULL;
		if (file) Remember(file, List[0]);
		return file;
	}
	void Return_File(FileClass *file) override
	{
		for (int i = 0; i < 64; i++) {
			if (Out[i].file == file) {
				Out[i].file = NULL;
				Out[i].owner->Return_File(file);
				return;
			}
		}
		delete file;   /* not tracked (more than 64 open at once): factories' files are plain objects */
	}
private:
	void Remember(FileClass *file, FileFactoryClass *owner)
	{
		for (int i = 0; i < 64; i++) {
			if (!Out[i].file) { Out[i].file = file; Out[i].owner = owner; return; }
		}
	}
	FileFactoryClass *List[8];
	int Count;
	struct { FileClass *file; FileFactoryClass *owner; } Out[64];
};

static SimpleFileFactoryClass LooseFiles;
static ChainFileFactory GameFiles;

static void Set_Up_Game_Files(void)
{
	GameFiles.Add(&LooseFiles);
	/* the game adds level archives after the always archives */
	static const char *const archives[] = { "Always2.dat", "Always.dbs", "Always.dat", "M00_Tutorial.mix" };
	for (const char *name : archives) {
		MixFileFactoryClass *mix = new MixFileFactoryClass(name, &LooseFiles);
		if (mix->Is_Valid()) {
			GameFiles.Add(mix);
			debugPrint("  archive %s: opened\n", name);
		} else {
			delete mix;
			debugPrint("  archive %s: not on the disc\n", name);
		}
	}
	_TheFileFactory = &GameFiles;
	WW3DAssetManager::Get_Instance()->Set_WW3D_Load_On_Demand(true);   /* fetch referenced files by name */
}

/* Models shown with the engine's own scene, camera and WW3D::Render, each with its own fly
** camera (Renegade's world has Z up): the Nod Minigunner from always.dat, and the tutorial
** level's terrain (with its lightmaps) from M00_Tutorial.mix. */
struct FlyCamera { Vector3 eye; float yaw, pitch; };

struct ModelView {
	const char *name;          /* render object name, as the game asks for it */
	const char *label;
	float speed_per_radius;    /* fly speed per second, in bounding-sphere radii */
	bool turns;                /* turns on its own until the camera is moved */
	Vector3 start_offset;      /* starting eye position, in radii from the sphere's center */
	bool is_level;             /* a whole level, loaded through wwphys's physics scene */
	RenderObjClass *object;    /* the model (none for a level) */
	SceneClass *scene;
	CameraClass *camera;
	SphereClass sphere;
	FlyCamera start, fly;
	bool flying;
	float angle;
	int memory_kb;             /* cost of loading it, from the free-memory readout */
	int definitions, statics;  /* level only: definitions loaded, static objects placed */
	int statics_with_model;    /* level only: placed objects that have a model to draw */
	Vector3 extent_lo, extent_hi;
	bool vis;                  /* level only: visibility culling on (the engine's default) */
};

enum { MODEL_COUNT = 4 };
static ModelView Models[MODEL_COUNT] = {
	{ "c_nod_mg_", "c_nod_mg_ (Nod Minigunner)", 1.5f, true, Vector3(0.0f, -2.6f, 0.6f), false },
	{ "tut_lm015", "tut_lm015 (tutorial terrain)", 0.25f, false, Vector3(0.0f, -0.8f, 0.35f), false },
	{ "m00_tutorial.lsd", "M00 tutorial level (wwphys)", 0.25f, false, Vector3(0.0f, -0.8f, 0.35f), true },
	{ "c_havoc_", "c_havoc_ (Havoc)", 1.5f, true, Vector3(0.0f, -2.6f, 0.6f), false },
};
enum { MODEL_MINIGUNNER, MODEL_TERRAIN, MODEL_LEVEL, MODEL_HAVOC };
enum { SCENE_FIRST_MODEL = 2 + 3 /* cube, planes, three textured cubes */ };

/* Scenes each test cycles through with Y (scene numbers: 0 cube, 1 planes, 2-4 textured cubes,
** then one per model). -1 ends a list. */
static const int TestScenes[TEST_COUNT][6] = {
	{ -1 },
	{ 0, 1, 2, 3, 4, -1 },
	{ SCENE_FIRST_MODEL + MODEL_MINIGUNNER, SCENE_FIRST_MODEL + MODEL_TERRAIN, -1 },
	{ SCENE_FIRST_MODEL + MODEL_LEVEL, -1 },
	{ SCENE_FIRST_MODEL + MODEL_HAVOC, -1 },
};
static bool Test_Uses_Model(int model)
{
	for (int i = 0; TestScenes[Test][i] >= 0; i++) if (TestScenes[Test][i] == SCENE_FIRST_MODEL + model) return true;
	return false;
}

/* --- Animation test ---------------------------------------------------------------------
** Renegade's human movement animations, by the names the game builds them with
** (Combat/humanstate.cpp): S_A_HUMAN.H_A_ + torso code (A0: empty hands) + leg code. Each is
** loaded on demand from its own file (h_a_a0a1.w3d...); a name that isn't found is shown as
** such. Switching blends from the old animation to the new over a quarter second (HTree blend). */
struct AnimEntry { const char *name; const char *label; HAnimClass *anim; int memory_kb; };
static AnimEntry Anims[] = {
	{ "S_A_HUMAN.H_A_A0A0", "stand" },
	{ "S_A_HUMAN.H_A_A0A0_L01", "idle fidget" },
	{ "S_A_HUMAN.H_A_A0A1", "run forward" },
	{ "S_A_HUMAN.H_A_A0A2", "run backward" },
	{ "S_A_HUMAN.H_A_A0A3", "run left" },
	{ "S_A_HUMAN.H_A_A0A4", "run right" },
	{ "S_A_HUMAN.H_A_A0A5", "turn left" },
	{ "S_A_HUMAN.H_A_A0A6", "turn right" },
	{ "S_A_HUMAN.H_A_A0B1", "walk forward" },
	{ "S_A_HUMAN.H_A_A0B2", "walk backward" },
	{ "S_A_HUMAN.H_A_A0B3", "walk left" },
	{ "S_A_HUMAN.H_A_A0B4", "walk right" },
	{ "S_A_HUMAN.H_A_A0C0", "crouch" },
	{ "S_A_HUMAN.H_A_A0C1", "crouch forward" },
	{ "S_A_HUMAN.H_A_A0C5", "crouch turn" },
	{ "S_A_HUMAN.H_A_A0J0", "jump up" },
	{ "S_A_HUMAN.H_A_A0J1", "jump forward" },
	{ "S_A_HUMAN.H_A_412A", "climb (ladder)" },
	{ "S_A_HUMAN.H_A_DIV1", "dive" },
};
enum { ANIM_COUNT = sizeof(Anims) / sizeof(Anims[0]) };
static int AnimsFound, AnimsKB;

static unsigned Free_Memory_KB(void);
static void Load_Animations(void)
{
	for (int i = 0; i < ANIM_COUNT; i++) {
		unsigned before = Free_Memory_KB();
		Anims[i].anim = WW3DAssetManager::Get_Instance()->Get_HAnim(Anims[i].name);   /* keeps a reference */
		Anims[i].memory_kb = (int)before - (int)Free_Memory_KB();
		if (Anims[i].anim) {
			AnimsFound++;
			AnimsKB += Anims[i].memory_kb;
			debugPrint("  anim %s: %d frames at %d fps, %d KB\n", Anims[i].name, Anims[i].anim->Get_Num_Frames(),
			           (int)Anims[i].anim->Get_Frame_Rate(), Anims[i].memory_kb);
		} else {
			debugPrint("  anim %s: NOT FOUND\n", Anims[i].name);
		}
	}
	debugPrint("  animations: %d of %d found, %d KB\n", AnimsFound, (int)ANIM_COUNT, AnimsKB);
}

/* Building interiors and doors from M00_Tutorial.mix, added to the terrain's scene exactly as
** stored (no positioning). Their lightmaps were baked for this level, so they may already be
** modeled in the level's own coordinates and fill the doorways; if they were modeled around their
** own origin instead, they'll gather at the level's center and need the level's placement data. */
static const char *const TerrainExtraNames[] = {
	"mgagd_int_lm003", "mgbar_int_lm001", "mgpwr_int_lm001", "mgref_int_lm002", "mgwep_int_lm003",
	"mgagd_doors_t", "mgbar_doors_t", "mgpwr_doors_t", "mgref_doors_t", "mgwep_doors_t" };
enum { TERRAIN_EXTRAS = sizeof(TerrainExtraNames) / sizeof(TerrainExtraNames[0]) };
static RenderObjClass *TerrainExtras[TERRAIN_EXTRAS];
static int TerrainExtrasLoaded, TerrainExtrasKB;
static bool TerrainExtrasShown = true;

static Vector3 Fly_Direction(const FlyCamera &c)
{
	return Vector3(cosf(c.pitch) * cosf(c.yaw), cosf(c.pitch) * sinf(c.yaw), sinf(c.pitch));
}

static void Apply_Fly_Camera(ModelView &m)
{
	Matrix3D tm(true);
	tm.Look_At(m.fly.eye, m.fly.eye + Fly_Direction(m.fly), 0.0f);
	m.camera->Set_Transform(tm);
}

static unsigned Free_Memory_KB(void);
static void Set_Up_Fly_Camera(ModelView &m);

/* Loads a save/load file (definitions, level data) as the game does (SaveGameManager in Combat):
** chunk by chunk, each chunk going to the subsystem registered for it; chunks for code that isn't
** ported (Combat, audio) have no subsystem and are skipped. */
static bool Load_Save_Load_File(const char *name, bool auto_post_load)
{
	FileClass *file = _TheFileFactory->Get_File(name);
	if (!file) return false;
	bool ok = false;
	if (file->Is_Available()) {
		file->Open(FileClass::READ);
		ChunkLoadClass cload(file);
		ok = SaveLoadSystemClass::Load(cload, auto_post_load);
		file->Close();
	}
	_TheFileFactory->Return_File(file);
	return ok;
}

/* The level, the game's way: WWPhys::Init, a PhysicsSceneClass (Combat makes exactly this),
** the definitions from objects.ddb (in always.dbs; only wwphys's ~1,900 of its 15,000 load
** here), then the level's static data (.lsd) and the post-load step that links each placed
** object to its definition and model. */
static void Load_Level(ModelView &m)
{
	unsigned before = Free_Memory_KB();
	WWPhys::Init();
	PhysicsSceneClass *scene = new PhysicsSceneClass;

	/* Exactly as Combat sets up its scene (combat.cpp, right after creating it): lighting, fog and
	** which collision groups collide. Without this every group pair stays "don't collide", so
	** every ray cast (including the VIS sector lookup) finds nothing. Group numbers from
	** Combat/combat.h; COLLISION_GROUP_WORLD is wwphys's 15, the same as TERRAIN. */
	enum { DEFAULT_GROUP = 0, UNCOLLIDEABLE = 1, TERRAIN_ONLY = 2, BULLET = 3, TERRAIN_AND_BULLET = 4,
	       BULLET_ONLY = 5, SOLDIER = 6, SOLDIER_GHOST = 7, TERRAIN = 15 };
	const int WORLD = PhysicsSceneClass::COLLISION_GROUP_WORLD;
	scene->Set_Ambient_Light(Vector3(0.55f, 0.55f, 0.55f));
	scene->Set_Ambient_Light(Vector3(1, 1, 1));
	scene->Set_Fog_Color(Vector3(0.6f, 0.6f, 0.6f));
	scene->Enable_All_Collision_Detections(DEFAULT_GROUP);
	scene->Enable_All_Collision_Detections(BULLET);
	scene->Enable_All_Collision_Detections(TERRAIN);
	scene->Enable_All_Collision_Detections(WORLD);
	scene->Enable_All_Collision_Detections(SOLDIER_GHOST);
	scene->Enable_All_Collision_Detections(SOLDIER);
	scene->Disable_All_Collision_Detections(UNCOLLIDEABLE);
	scene->Disable_All_Collision_Detections(TERRAIN_ONLY);
	scene->Disable_All_Collision_Detections(TERRAIN_AND_BULLET);
	scene->Disable_All_Collision_Detections(BULLET_ONLY);
	scene->Enable_Collision_Detection(TERRAIN_ONLY, TERRAIN);
	scene->Enable_Collision_Detection(TERRAIN_AND_BULLET, TERRAIN);
	scene->Enable_Collision_Detection(TERRAIN_AND_BULLET, BULLET);
	scene->Disable_Collision_Detection(BULLET, BULLET);
	scene->Enable_Collision_Detection(BULLET_ONLY, BULLET);
	scene->Disable_Collision_Detection(WORLD, WORLD);
	scene->Disable_Collision_Detection(SOLDIER_GHOST, SOLDIER);
	scene->Disable_Collision_Detection(SOLDIER_GHOST, SOLDIER_GHOST);
	debugPrint("  level: loading objects.ddb (definitions) ...\n");
	bool defs_ok = Load_Save_Load_File("objects.ddb", true);
	for (DefinitionClass *d = DefinitionMgrClass::Get_First(); d; d = DefinitionMgrClass::Get_Next(d)) m.definitions++;
	debugPrint("  level: objects.ddb %s, %d definitions, %u KB free\n", defs_ok ? "loaded" : "NOT LOADED",
	           m.definitions, Free_Memory_KB());
	debugPrint("  level: loading %s ...\n", m.name);
	bool lsd_ok = Load_Save_Load_File(m.name, false);
	SaveLoadSystemClass::Post_Load_Processing(NULL);
	RefPhysListIterator it = scene->Get_Static_Object_Iterator();
	for (it.First(); !it.Is_Done(); it.Next()) {
		m.statics++;
		if (it.Peek_Obj()->Peek_Model()) m.statics_with_model++;
	}
	m.memory_kb = (int)before - (int)Free_Memory_KB();
	debugPrint("  level: %s %s, %d static objects, %d KB, %u KB free\n", m.name, lsd_ok ? "loaded" : "NOT LOADED",
	           m.statics, m.memory_kb, Free_Memory_KB());
	m.scene = scene;
	Vector3 lo, hi;
	scene->Get_Level_Extents(lo, hi);
	m.extent_lo = lo;
	m.extent_hi = hi;
	m.vis = true;
	debugPrint("  level: extents (%d, %d, %d) to (%d, %d, %d)\n", (int)lo.X, (int)lo.Y, (int)lo.Z,
	           (int)hi.X, (int)hi.Y, (int)hi.Z);
	/* Start the camera where we know the level is: around the tutorial terrain, measured when it
	** was loaded as a model (the extents above are shown for comparison). */
	if (Models[MODEL_TERRAIN].object) {
		m.sphere = Models[MODEL_TERRAIN].sphere;
	} else {
		m.sphere.Center.Set(5.0f, 4.0f, 18.0f);   /* tut_lm015, as measured by the models test */
		m.sphere.Radius = 163.0f;
	}
	Set_Up_Fly_Camera(m);
}

/* Collision probe: the same downward search the VIS lookup does (StaticAABTreeCullClass::
** Find_Vis_Tile casts 20 units straight down against VIS collision objects), against a given
** collision type. Returns the distance to the hit, or -1, and the name of the model hit. */
static float Probe_Down(PhysicsSceneClass *scene, const Vector3 &from, int type, const char **hit_name)
{
	CastResultStruct result;
	LineSegClass ray(from, from - Vector3(0.0f, 0.0f, 20.0f));
	PhysRayCollisionTestClass test(ray, &result, 0, type);
	test.CheckDynamicObjs = false;
	scene->Cast_Ray(test);
	if (result.Fraction >= 1.0f) return -1.0f;
	if (hit_name && test.CollidedPhysObj && test.CollidedPhysObj->Peek_Model()) {
		*hit_name = test.CollidedPhysObj->Peek_Model()->Get_Name();
	}
	return result.Fraction * 20.0f;
}

static void Load_Model(ModelView &m)
{
	unsigned before = Free_Memory_KB();
	debugPrint("  loading %s through the asset manager ...\n", m.name);
	m.object = WW3DAssetManager::Get_Instance()->Create_Render_Obj(m.name);
	unsigned after = Free_Memory_KB();
	m.memory_kb = (int)before - (int)after;
	if (!m.object) {
		debugPrint("  %s: not found (see the lines above)\n", m.name);
		return;
	}
	m.sphere = m.object->Get_Bounding_Sphere();
	debugPrint("  %s loaded: %d KB, radius %d at (%d, %d, %d), %u KB free\n", m.name, m.memory_kb,
	           (int)m.sphere.Radius, (int)m.sphere.Center.X, (int)m.sphere.Center.Y, (int)m.sphere.Center.Z, after);

	m.scene = new SimpleSceneClass();
	m.scene->Set_Ambient_Light(Vector3(0.45f, 0.45f, 0.45f));
	/* (the level's lighting comes from its own data instead) */
	LightClass *sun = new LightClass(LightClass::DIRECTIONAL);
	sun->Set_Diffuse(Vector3(0.9f, 0.9f, 0.85f));
	Matrix3D sun_tm(true);
	sun_tm.Look_At(Vector3(5.0f, -5.0f, 8.0f), Vector3(0.0f, 0.0f, 0.0f), 0.0f);
	sun->Set_Transform(sun_tm);
	m.scene->Add_Render_Object(sun);
	m.scene->Add_Render_Object(m.object);

	Set_Up_Fly_Camera(m);
}

static void Set_Up_Fly_Camera(ModelView &m)
{
	float r = m.sphere.Radius > 0.01f ? m.sphere.Radius : 1.0f;
	m.camera = new CameraClass();
	m.camera->Set_View_Plane(DEG_TO_RADF(50.0f));
	m.camera->Set_Clip_Planes(r * 0.002f > 0.05f ? r * 0.002f : 0.05f, r * 4.0f);   /* scaled to the model */
	Vector3 target = m.sphere.Center;
	Vector3 eye = target + m.start_offset * r;
	Vector3 to_target = target - eye;
	m.start.eye = eye;
	m.start.yaw = atan2f(to_target.Y, to_target.X);
	m.start.pitch = atan2f(to_target.Z, sqrtf(to_target.X * to_target.X + to_target.Y * to_target.Y));
	m.fly = m.start;
	Apply_Fly_Camera(m);
}

static void Fill_Cube(DX8VertexBufferClass *vb, DX8IndexBufferClass *ib)
{
	static const float corners[8][3] = {
		{ -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
		{ -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 } };
	static const unsigned colors[8] = {
		0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00,
		0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF, 0xFF808080 };
	/* Each face counterclockwise on screen as seen from outside: Renegade's convention (the
	** engine culls clockwise). Checked by projecting the faces through the test camera. */
	static const unsigned short faces[36] = {
		0, 1, 2, 0, 2, 3,   4, 6, 5, 4, 7, 6,   0, 3, 7, 0, 7, 4,
		1, 5, 6, 1, 6, 2,   3, 2, 6, 3, 6, 7,   0, 4, 5, 0, 5, 1 };
	{
		VertexBufferClass::WriteLockClass lock(vb);
		VertexFormatXYZDUV1 *v = (VertexFormatXYZDUV1 *)lock.Get_Vertex_Array();
		for (int i = 0; i < 8; i++) {
			v[i].x = corners[i][0]; v[i].y = corners[i][1]; v[i].z = corners[i][2];
			v[i].diffuse = colors[i];
			v[i].u1 = 0.0f; v[i].v1 = 0.0f;
		}
	}
	{
		IndexBufferClass::WriteLockClass lock(ib);
		unsigned short *idx = lock.Get_Index_Array();
		for (int i = 0; i < 36; i++) idx[i] = faces[i];
	}
}

static void Create_Test_Geometry(void)
{
	CubeVB = new DX8VertexBufferClass(DX8_FVF_XYZDUV1, 8);
	CubeIB = new DX8IndexBufferClass(36);
	Fill_Cube(CubeVB, CubeIB);

	/* Two 3x3 squares through the origin: one facing the camera (red), one turned 60 degrees
	** about the vertical axis (blue). They meet along the vertical line through the center. */
	PlanesVB = new DX8VertexBufferClass(DX8_FVF_XYZDUV1, 8);
	PlanesIB = new DX8IndexBufferClass(12);
	{
		VertexBufferClass::WriteLockClass lock(PlanesVB);
		VertexFormatXYZDUV1 *v = (VertexFormatXYZDUV1 *)lock.Get_Vertex_Array();
		const float c = 0.5f, sn = 0.8660254f;   /* cos and sin of 60 degrees */
		const float xs[4] = { -1.5f, 1.5f, 1.5f, -1.5f }, ys[4] = { -1.5f, -1.5f, 1.5f, 1.5f };
		for (int i = 0; i < 4; i++) {
			v[i].x = xs[i]; v[i].y = ys[i]; v[i].z = 0.0f; v[i].diffuse = 0xFFE03030;
			v[4 + i].x = xs[i] * c; v[4 + i].y = ys[i]; v[4 + i].z = xs[i] * sn; v[4 + i].diffuse = 0xFF3050E0;
		}
		for (int i = 0; i < 8; i++) { v[i].u1 = 0.0f; v[i].v1 = 0.0f; }
	}
	{
		IndexBufferClass::WriteLockClass lock(PlanesIB);
		static const unsigned short quads[12] = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
		unsigned short *idx = lock.Get_Index_Array();
		for (int i = 0; i < 12; i++) idx[i] = quads[i];
	}

	/* Textured cube: four vertices per face, so each face gets the whole texture. */
	static const int face_corners[6][4] = {
		{ 0, 3, 2, 1 }, { 5, 6, 7, 4 }, { 4, 7, 3, 0 }, { 1, 2, 6, 5 }, { 3, 7, 6, 2 }, { 4, 0, 1, 5 } };
	static const float corner_pos[8][3] = {
		{ -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
		{ -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 } };
	TexCubeIB = new DX8IndexBufferClass(36);
	for (int t = 0; t < TEXTURE_SCENES; t++) {
		const float repeat = TextureRepeat[t];   /* coordinates past 1 tile the texture (wrap mode) */
		TexCubeVB[t] = new DX8VertexBufferClass(DX8_FVF_XYZDUV1, 24);
		VertexBufferClass::WriteLockClass lock(TexCubeVB[t]);
		VertexFormatXYZDUV1 *v = (VertexFormatXYZDUV1 *)lock.Get_Vertex_Array();
		static const float uv[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
		for (int f = 0; f < 6; f++) {
			for (int k = 0; k < 4; k++) {
				VertexFormatXYZDUV1 &out = v[f * 4 + k];
				const float *c = corner_pos[face_corners[f][k]];
				out.x = c[0]; out.y = c[1]; out.z = c[2];
				out.diffuse = 0xFFFFFFFF;
				out.u1 = uv[k][0] * repeat; out.v1 = uv[k][1] * repeat;
			}
		}
	}
	{
		IndexBufferClass::WriteLockClass lock(TexCubeIB);
		unsigned short *idx = lock.Get_Index_Array();
		for (int f = 0; f < 6; f++) {
			/* counterclockwise on screen from outside, like the vertex-colored cube (checked by
			** projecting the faces through the test camera) */
			const unsigned short b = (unsigned short)(f * 4);
			unsigned short tri[6] = { b, (unsigned short)(b + 2), (unsigned short)(b + 1),
			                          b, (unsigned short)(b + 3), (unsigned short)(b + 2) };
			for (int k = 0; k < 6; k++) idx[f * 6 + k] = tri[k];
		}
	}
	for (int t = 0; t < TEXTURE_SCENES; t++) {
		Textures[t] = WW3DAssetManager::Get_Instance()->Get_Texture(TextureNames[t]);
	}

	VertexColorMaterial = new VertexMaterialClass();
	VertexColorMaterial->Set_Lighting(false);
	VertexColorMaterial->Set_Diffuse_Color_Source(VertexMaterialClass::COLOR1);
}

static void Draw_Geometry(DX8VertexBufferClass *vb, DX8IndexBufferClass *ib, unsigned short triangles,
                          unsigned short vertices, const Matrix3D &world, bool cull,
                          TextureClass *texture = NULL, bool alpha = false)
{
	Matrix3D view(true);
	view.Set_Translation(Vector3(0.0f, 0.0f, 5.0f));

	/* Direct3D-style perspective projection (left-handed, depth 0..1), written in the
	** engine's convention, which is the transpose of Direct3D's (Set_Transform transposes). */
	const float zn = 0.5f, zf = 100.0f, ys = 1.0f / tanf(0.5f * 1.0f), xs = ys / (640.0f / 480.0f);
	Matrix4 proj(true);
	proj[0] = Vector4(xs, 0, 0, 0);
	proj[1] = Vector4(0, ys, 0, 0);
	proj[2] = Vector4(0, 0, zf / (zf - zn), -zn * zf / (zf - zn));
	proj[3] = Vector4(0, 0, 1, 0);

	ShaderClass shader = texture ? (alpha ? ShaderClass::_PresetAlphaShader : ShaderClass::_PresetOpaqueShader)
	                             : ShaderClass::_PresetOpaqueSolidShader;
	shader.Set_Cull_Mode(cull ? ShaderClass::CULL_MODE_ENABLE : ShaderClass::CULL_MODE_DISABLE);

	DX8Wrapper::Set_Transform(D3DTS_WORLD, world);
	DX8Wrapper::Set_Transform(D3DTS_VIEW, view);
	DX8Wrapper::Set_Transform(D3DTS_PROJECTION, proj);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Set_Texture(0, texture);
	DX8Wrapper::Set_Material(VertexColorMaterial);
	DX8Wrapper::Set_Vertex_Buffer(vb);
	DX8Wrapper::Set_Index_Buffer(ib, 0);
	DX8Wrapper::Draw_Triangles(0, triangles, 0, vertices);
}

/* Churn: create the cube's buffers and a 64x64 texture through the engine, use them, release them. */
static void Churn_Frame(const Matrix3D &world, bool cull)
{
	DX8VertexBufferClass *vb = new DX8VertexBufferClass(DX8_FVF_XYZDUV1, 8);
	DX8IndexBufferClass *ib = new DX8IndexBufferClass(36);
	Fill_Cube(vb, ib);
	Draw_Geometry(vb, ib, 12, 8, world, cull);
	REF_PTR_RELEASE(vb);   /* DX8Wrapper keeps its own reference until the next buffer is set */
	REF_PTR_RELEASE(ib);

	IDirect3DTexture8 *texture = DX8Wrapper::_Create_DX8_Texture(64, 64, WW3D_FORMAT_A8R8G8B8, TextureClass::MIP_LEVELS_ALL);
	if (texture) {
		D3DLOCKED_RECT locked;
		if (SUCCEEDED(texture->LockRect(0, &locked, NULL, 0))) {
			memset(locked.pBits, 0x80, locked.Pitch * 64);
			texture->UnlockRect(0);
		}
		texture->Release();
	}
}

/* --- Controller (nxdk's SDL2), and memory ----------------------------------------------- */

static SDL_GameController *Pad;
enum { BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_START, BUTTON_BACK, BUTTON_BLACK, BUTTON_WHITE, BUTTON_DPAD_LEFT,
       BUTTON_DPAD_RIGHT, BUTTON_DPAD_UP, BUTTON_DPAD_DOWN, BUTTON_COUNT };

/* Returns which buttons were pressed since the last call (press, not hold). */
static void Poll_Buttons(bool pressed[BUTTON_COUNT])
{
	static bool held[BUTTON_COUNT];
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_CONTROLLERDEVICEADDED && !Pad) {
			Pad = SDL_GameControllerOpen(e.cdevice.which);
		} else if (e.type == SDL_CONTROLLERDEVICEREMOVED && Pad &&
		           e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(Pad))) {
			SDL_GameControllerClose(Pad);
			Pad = NULL;
		}
	}
	SDL_GameControllerUpdate();
	static const SDL_GameControllerButton map[BUTTON_COUNT] = {
		SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
		SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
		SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,     /* the Black button on an original Xbox controller */
		SDL_CONTROLLER_BUTTON_LEFTSHOULDER,      /* the White button */
		SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_UP,
		SDL_CONTROLLER_BUTTON_DPAD_DOWN };
	for (int i = 0; i < BUTTON_COUNT; i++) {
		bool now = Pad && SDL_GameControllerGetButton(Pad, map[i]);
		pressed[i] = now && !held[i];
		held[i] = now;
	}
}

/* True while Start and Back are both held: back to the menu. */
static bool Menu_Combo_Held(void)
{
	return Pad && SDL_GameControllerGetButton(Pad, SDL_CONTROLLER_BUTTON_START) &&
	       SDL_GameControllerGetButton(Pad, SDL_CONTROLLER_BUTTON_BACK);
}

/* The boot menu, on the text screen before the 3D device starts. Doesn't return: choosing a
** test relaunches the program. */
static void Run_Menu(void)
{
	int choice = TEST_RENDER;
	bool redraw = true;
	for (;;) {
		bool pressed[BUTTON_COUNT];
		Poll_Buttons(pressed);
		if (pressed[BUTTON_DPAD_UP] && choice > TEST_RENDER) { choice--; redraw = true; }
		if (pressed[BUTTON_DPAD_DOWN] && choice < TEST_COUNT - 1) { choice++; redraw = true; }
		if (redraw) {
			debugClearScreen();
			debugPrint("\n  Renegade on Xbox - test disc\n\n");
			debugPrint("  Each test starts on a freshly started machine.\n");
			debugPrint("  Free memory now: %u KB\n\n", Free_Memory_KB());
			for (int t = TEST_RENDER; t < TEST_COUNT; t++) {
				debugPrint("  %s %s\n", t == choice ? ">>" : "  ", TestNames[t]);
			}
			debugPrint("\n  D-pad up/down choose, A start\n");
			debugPrint("  In a test: Start + Back together returns here\n");
			debugPrint(Pad ? "\n" : "\n  (waiting for a controller)\n");
			redraw = false;
		}
		static bool had_pad;
		if ((Pad != NULL) != had_pad) { had_pad = Pad != NULL; redraw = true; }
		if (pressed[BUTTON_A]) Relaunch(choice);
		Sleep(16);
	}
}

/* A stick or trigger as -1..1 (triggers 0..1), with a dead zone so a resting stick reads 0. */
static float Axis(SDL_GameControllerAxis axis)
{
	if (!Pad) return 0.0f;
	float v = SDL_GameControllerGetAxis(Pad, axis) / 32767.0f;
	const float dead = 0.2f;
	if (v > -dead && v < dead) return 0.0f;
	return v > 0 ? (v - dead) / (1.0f - dead) : (v + dead) / (1.0f - dead);
}

/* Left stick flies and strafes, right stick looks, right/left trigger rise/descend. */
static void Update_Fly_Camera(ModelView &m, float seconds)
{
	float move_x = Axis(SDL_CONTROLLER_AXIS_LEFTX), move_y = -Axis(SDL_CONTROLLER_AXIS_LEFTY);
	float look_x = Axis(SDL_CONTROLLER_AXIS_RIGHTX), look_y = -Axis(SDL_CONTROLLER_AXIS_RIGHTY);
	float lift = Axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) - Axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	if (move_x == 0 && move_y == 0 && look_x == 0 && look_y == 0 && lift == 0) return;
	m.flying = true;
	const float turn_rate = 1.8f;                                     /* radians per second */
	float speed = m.speed_per_radius * (m.sphere.Radius > 0.01f ? m.sphere.Radius : 1.0f);
	m.fly.yaw -= look_x * turn_rate * seconds;                        /* stick right: turn right */
	m.fly.pitch += look_y * turn_rate * seconds;
	const float limit = DEG_TO_RADF(85.0f);
	if (m.fly.pitch > limit) m.fly.pitch = limit;
	if (m.fly.pitch < -limit) m.fly.pitch = -limit;
	Vector3 forward = Fly_Direction(m.fly);
	Vector3 right(sinf(m.fly.yaw), -cosf(m.fly.yaw), 0.0f);
	m.fly.eye += (forward * move_y + right * move_x) * (speed * seconds);
	m.fly.eye.Z += lift * speed * seconds;
	Apply_Fly_Camera(m);
}

static unsigned Free_Memory_KB(void)
{
	MM_STATISTICS stats;
	memset(&stats, 0, sizeof(stats));
	stats.Length = sizeof(stats);
	MmQueryStatistics(&stats);
	return stats.AvailablePages * 4;
}

/* --- Start-up messages ----------------------------------------------------------------
** Progress markers from the port's Direct3D layer, and the engine's own messages, are printed
** on the text screen. The layer keeps that screen visible until the first frame is presented,
** so if anything stops before then, the last line shows where. */
static void __cdecl trace_to_screen(const char *message)
{
	debugPrint("    %s\n", message);
}

static void engine_message(DebugType, const char *message)
{
	debugPrint("    engine: %s", message);
}

/* Runs before every C++ global constructor (the C initializer list, as in the smoke test), so
** a constructor that hangs before main() is still visible: the last start-up line shows where. */
extern "C" {
static int __cdecl bringup_early_init(void)
{
	XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
	debugPrint("Renegade Xbox port - render bring-up (step 2)\n\n");
	debugPrint("  start-up: early init reached (before C++ constructors)\n");
	XboxPort_Trace = trace_to_screen;
	return 0;
}
__attribute__((section(".CRT$XIU"), used))
int (__cdecl *const bringup_early_init_p)(void) = bringup_early_init;
}

static void fail(const char *what, bool device_started)
{
	if (device_started) pb_show_debug_screen();
	debugPrint("\nFAILED: %s\n", what);
	while (1) Sleep(1000);
}

int main(void)
{
	debugPrint("  start-up: main() reached (all global constructors done)\n\n");
	if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) debugPrint("  SDL_Init failed (no controller): %s\n", SDL_GetError());
	Test = Launched_Test();
	if (Test == TEST_MENU) Run_Menu();
	debugPrint("  test: %s\n", TestNames[Test]);
	WWDebug_Install_Message_Handler(engine_message);

	/* Every program using the engine creates the asset manager before WW3D::Init (the game
	** creates its own; EA's test programs declare a WW3DAssetManager). WW3D doesn't create one,
	** and its subsystems use it during start-up. */
	debugPrint("  creating the asset manager\n");
	new WW3DAssetManager;
	Set_Up_Game_Files();

	debugPrint("  WW3D::Init ...\n");
	if (WW3D::Init(NULL) != WW3D_ERROR_OK) fail("WW3D::Init", false);
	debugPrint("  WW3D::Init ok: %d render device(s), first: %s\n", WW3D::Get_Render_Device_Count(),
	           WW3D::Get_Render_Device_Count() > 0 ? WW3D::Get_Render_Device_Name(0) : "(none)");
	if (WW3D::Get_Render_Device_Count() < 1) fail("no render devices enumerated", false);

	debugPrint("  WW3D::Set_Render_Device(0, 640x480, 32-bit) ...\n");
	if (WW3D::Set_Render_Device(0, 640, 480, 32, 0) != WW3D_ERROR_OK) fail("WW3D::Set_Render_Device", false);
	debugPrint("  WW3D::Set_Render_Device ok; rendering the first frames ...\n");
	Sleep(2000);   /* time to read the lines above; the first presented frame replaces them */

	if (Test == TEST_RENDER) {
		Create_Test_Geometry();
		debugPrint("  test geometry created (engine vertex and index buffers)\n");
	}
	for (int i = 0; i < MODEL_COUNT; i++) {
		if (!Test_Uses_Model(i)) continue;
		if (Models[i].is_level) Load_Level(Models[i]);
		else Load_Model(Models[i]);
	}
	if (Test == TEST_ANIMATION) Load_Animations();
	ModelView &terrain = Models[MODEL_TERRAIN];
	if (terrain.object) {
		for (int i = 0; i < TERRAIN_EXTRAS; i++) {
			unsigned before = Free_Memory_KB();
			TerrainExtras[i] = WW3DAssetManager::Get_Instance()->Create_Render_Obj(TerrainExtraNames[i]);
			int cost = (int)before - (int)Free_Memory_KB();
			if (TerrainExtras[i]) {
				terrain.scene->Add_Render_Object(TerrainExtras[i]);   /* identity transform: as stored */
				TerrainExtrasLoaded++;
				TerrainExtrasKB += cost;
				SphereClass b = TerrainExtras[i]->Get_Bounding_Sphere();
				debugPrint("  %s: %d KB, center (%d, %d, %d)\n", TerrainExtraNames[i], cost,
				           (int)b.Center.X, (int)b.Center.Y, (int)b.Center.Z);
			} else {
				debugPrint("  %s: not found\n", TerrainExtraNames[i]);
			}
		}
	}
	bool churn = false, cull = true, w_one = false;
	enum { TEXT_FULL, TEXT_COMPACT, TEXT_OFF } text_mode = TEXT_FULL;
	int scene_slot = 0;                         /* position in this test's scene list */
	int scene = TestScenes[Test][0];            /* 0 cube, 1 planes, 2-4 textured, then models */
	int anim_index = 0, prev_anim = -1;         /* animation test: current and blended-from */
	float anim_frame = 0, prev_frame = 0, blend = 1.0f;
	bool blending = true;
	unsigned baseline_kb = 0, lowest_kb = 0;

	int w, h, bits;
	bool windowed;
	WW3D::Get_Render_Target_Resolution(w, h, bits, windowed);

	for (unsigned frame = 0;; frame++) {
		/* Cycle the clear color smoothly: three sine waves a third of a cycle apart. */
		float t = frame / 120.0f;
		Vector3 color(0.5f + 0.5f * sinf(t * 6.2832f),
		              0.5f + 0.5f * sinf(t * 6.2832f + 2.0944f),
		              0.5f + 0.5f * sinf(t * 6.2832f + 4.1888f));
		if (frame < 2) debugPrint("  frame %u: WW3D::Begin_Render\n", frame);
		/* A level is drawn the way the game's frame does it (Combat's Update, then Commando's
		** gamemode.cpp): Update the physics scene, Pre_Render_Processing (which builds the lists of
		** visible objects) before Begin_Render, and Post_Render_Processing after End_Render. */
		ModelView *level_view = NULL;
		{
			int i = scene - SCENE_FIRST_MODEL;
			if (i >= 0 && i < MODEL_COUNT && Models[i].is_level && Models[i].scene && Models[i].camera) {
				level_view = &Models[i];
			}
		}
		if (level_view) {
			PhysicsSceneClass *ps = static_cast<PhysicsSceneClass *>(level_view->scene);
			ps->Update(1.0f / 60.0f, (int)frame);
			ps->Pre_Render_Processing(*level_view->camera);
		}
		if (WW3D::Begin_Render(true, true, color) != WW3D_ERROR_OK) fail("WW3D::Begin_Render", frame > 0);

		bool pressed[BUTTON_COUNT];
		Poll_Buttons(pressed);
		if (pressed[BUTTON_A]) churn = !churn;
		if (pressed[BUTTON_B]) cull = !cull;
		if (Menu_Combo_Held()) Relaunch(TEST_MENU);
		if (pressed[BUTTON_Y]) {
			scene_slot = TestScenes[Test][scene_slot + 1] >= 0 ? scene_slot + 1 : 0;
			scene = TestScenes[Test][scene_slot];
		}
		if (pressed[BUTTON_BLACK]) text_mode = text_mode == TEXT_FULL ? TEXT_COMPACT :
		                                       (text_mode == TEXT_COMPACT ? TEXT_OFF : TEXT_FULL);
		bool planes = scene == 1;
		int model_index = scene - SCENE_FIRST_MODEL;
		bool model_scene = model_index >= 0;
		int textured = (scene >= 2 && !model_scene) ? scene - 2 : -1;
		if (pressed[BUTTON_START]) {
			w_one = !w_one;
			XboxD3D_Set_Diagnostic_W1(w_one);
		}

		float spin = frame / 60.0f;
		Matrix3D world(true);
		if (planes) {
			world.Rotate_Y(0.4f * sinf(spin * 0.5f));   /* swing gently so the meeting line moves */
			world.Rotate_X(0.3f);
		} else {
			world.Rotate_Y(spin);
			world.Rotate_X(spin * 0.7f);
		}
		if (model_scene) {
			ModelView &m = Models[model_index];
			if (m.scene && m.camera) {
				if (pressed[BUTTON_BACK]) {          /* reset the view; a turning model turns again */
					m.fly = m.start;
					m.flying = false;
					Apply_Fly_Camera(m);
				}
				Update_Fly_Camera(m, 1.0f / 60.0f);
				if (m.is_level && pressed[BUTTON_DPAD_LEFT]) {    /* the level's visibility culling on/off */
					m.vis = !m.vis;
					static_cast<PhysicsSceneClass *>(m.scene)->Enable_Vis(m.vis);
				}
				if (model_index == MODEL_TERRAIN && pressed[BUTTON_WHITE]) {   /* interiors and doors on/off */
					TerrainExtrasShown = !TerrainExtrasShown;
					for (int i = 0; i < TERRAIN_EXTRAS; i++) {
						if (TerrainExtras[i]) TerrainExtras[i]->Set_Hidden(!TerrainExtrasShown);
					}
				}
				if (model_index == MODEL_HAVOC && m.object) {
					/* D-pad left/right: previous/next animation; up: blending on/off. */
					int step = pressed[BUTTON_DPAD_RIGHT] ? 1 : (pressed[BUTTON_DPAD_LEFT] ? -1 : 0);
					if (pressed[BUTTON_DPAD_UP]) blending = !blending;
					if (step) {
						prev_anim = anim_index;
						prev_frame = anim_frame;
						anim_index = (anim_index + step + ANIM_COUNT) % ANIM_COUNT;
						anim_frame = 0;
						blend = blending ? 0.0f : 1.0f;
					}
					HAnimClass *cur = Anims[anim_index].anim;
					HAnimClass *old = (prev_anim >= 0) ? Anims[prev_anim].anim : NULL;
					const float dt = 1.0f / 60.0f;
					if (cur) {
						anim_frame += cur->Get_Frame_Rate() * dt;
						float n = (float)cur->Get_Num_Frames();
						if (n > 1 && anim_frame >= n - 1) anim_frame = fmodf(anim_frame, n - 1);
					}
					if (old && blend < 1.0f) {
						prev_frame += old->Get_Frame_Rate() * dt;
						float n = (float)old->Get_Num_Frames();
						if (n > 1 && prev_frame >= n - 1) prev_frame = fmodf(prev_frame, n - 1);
						blend += dt / 0.25f;
						if (blend > 1.0f) blend = 1.0f;
					}
					if (cur && old && blend < 1.0f) m.object->Set_Animation(old, prev_frame, cur, anim_frame, blend);
					else if (cur) m.object->Set_Animation(cur, anim_frame);
					else m.object->Set_Animation();   /* not found: the model's base pose */
				}
				if (m.object) {
					if (m.turns && !m.flying) m.angle += 0.5f / 60.0f;
					Matrix3D turn(true);
					turn.Rotate_Z(m.angle);             /* Z is up in Renegade's world */
					m.object->Set_Transform(turn);
				}
				WW3D::Render(m.scene, m.camera);
			}
		} else if (planes) {
			Draw_Geometry(PlanesVB, PlanesIB, 4, 8, world, false);   /* both sides of the planes */
		} else if (textured >= 0) {
			Draw_Geometry(TexCubeVB[textured], TexCubeIB, 12, 24, world, cull, Textures[textured], textured == 2);
		} else if (churn) {
			Churn_Frame(world, cull);
		} else {
			Draw_Geometry(CubeVB, CubeIB, 12, 8, world, cull);
		}

		/* Memory: the baseline is taken once start-up has settled (frame 300) or when X is
		** pressed. In an unchanged scene, free memory should stay flat; a steady fall is a leak. */
		unsigned free_kb = Free_Memory_KB();
		if (frame == 300 || pressed[BUTTON_X]) baseline_kb = lowest_kb = free_kb;
		if (baseline_kb && free_kb < lowest_kb) lowest_kb = free_kb;
		XboxD3DStats layer;
		XboxD3D_Get_Stats(&layer);

		/* Counted every frame, whether or not the text is shown. */
		static DWORD fps_start = GetTickCount();
		static unsigned fps_frames, fps;
		fps_frames++;
		if (GetTickCount() - fps_start >= 1000) {
			fps = fps_frames;
			fps_frames = 0;
			fps_start = GetTickCount();
		}

		/* On-screen text. pbkit's text overlay is a fixed 16 x 60 grid that scrolls when more than
		** 16 rows are printed, so every line stays under 60 characters and the memory figures come
		** first. Black cycles: full, compact (first two rows), off. */
		pb_erase_text_screen();
		if (text_mode != TEXT_OFF) {
			pb_print("F%u %ufps mem %uK", frame, fps, free_kb);
			if (baseline_kb) pb_print(" chg %+dK low %uK", (int)free_kb - (int)baseline_kb, lowest_kb);
			else pb_print(" (baseline at F300)");
			pb_print("\n");
			pb_print("D3D %utex %uK %uvb %uK %uib %uK %usurf\n", layer.textures, layer.texture_bytes / 1024,
			         layer.vertex_buffers, layer.vertex_bytes / 1024, layer.index_buffers, layer.index_bytes / 1024,
			         layer.surfaces);
		}
		if (text_mode == TEXT_FULL) {
			const char *scene_name = scene == 0 ? "cube" : (planes ? "crossing planes" :
			                         (model_scene ? Models[model_index].label : TextureNames[textured]));
			pb_print("%.34s cull:%s churn:%s\n", scene_name, planes ? "off" : (cull ? "on" : "off"),
			         churn ? "ON" : "off");
			if (textured >= 0) {
				IDirect3DTexture8 *d3d = Textures[textured] ? Textures[textured]->Peek_DX8_Texture() : NULL;
				D3DSURFACE_DESC desc;
				if (d3d && SUCCEEDED(d3d->GetLevelDesc(0, &desc))) {
					char fmt[5] = { 0 };
					if (desc.Format > 0xFF) memcpy(fmt, &desc.Format, 4);
					pb_print("Tex %s %ux%u %lu levels\n",
					         desc.Format > 0xFF ? fmt : (desc.Format == D3DFMT_A8R8G8B8 ? "A8R8G8B8" : "other"),
					         desc.Width, desc.Height, (unsigned long)d3d->GetLevelCount());
				} else {
					pb_print("Tex not loaded yet\n");
				}
			}
			if (model_scene) {
				ModelView &m = Models[model_index];
				if (m.is_level) {
					pb_print("%dK %d defs %d statics, %d with models\n", m.memory_kb, m.definitions, m.statics,
					         m.statics_with_model);
					pb_print("camera at %d %d %d\n", (int)m.fly.eye.X, (int)m.fly.eye.Y, (int)m.fly.eye.Z);
					pb_print("level X %d..%d Y %d..%d Z %d..%d\n", (int)m.extent_lo.X, (int)m.extent_hi.X,
					         (int)m.extent_lo.Y, (int)m.extent_hi.Y, (int)m.extent_lo.Z, (int)m.extent_hi.Z);
					/* The level's built-in visibility (VIS): precomputed tables, one per vis sector; each
					** frame the sector under the camera picks what may be drawn. Outside every sector
					** the last valid one is used, and with none yet there is no visibility culling. */
					PhysicsSceneClass *ps = static_cast<PhysicsSceneClass *>(m.scene);
					const PhysicsSceneClass::StatsStruct &st = ps->Get_Statistics();
					pb_print("VIS %d tables sect:%s%s cull in %d out %d\n", ps->Get_Vis_Table_Count(),
					         ps->Is_Vis_Sector_Missing() ? "NONE" : "ok",
					         ps->Is_Vis_Reset_Needed() ? " RESET" : "", st.CullNodesAccepted, st.CullNodesRejected);
					/* What lies below the camera, by collision type (distance, or "-" for nothing). */
					const char *hit_name = "";
					float d_phys = Probe_Down(ps, m.fly.eye, COLLISION_TYPE_PHYSICAL, NULL);
					float d_vis = Probe_Down(ps, m.fly.eye, COLLISION_TYPE_VIS, NULL);
					float d_all = Probe_Down(ps, m.fly.eye, COLLISION_TYPE_ALL, &hit_name);
					char a[12], b[12], c[12];
					if (d_phys < 0) strcpy(a, "-"); else snprintf(a, sizeof(a), "%d.%d", (int)d_phys, (int)(d_phys * 10) % 10);
					if (d_vis < 0) strcpy(b, "-"); else snprintf(b, sizeof(b), "%d.%d", (int)d_vis, (int)(d_vis * 10) % 10);
					if (d_all < 0) strcpy(c, "-"); else snprintf(c, sizeof(c), "%d.%d", (int)d_all, (int)(d_all * 10) % 10);
					pb_print("down: phys %s vis %s all %s %.18s\n", a, b, c, hit_name);
					pb_print("Sticks fly, triggers up/down, Back resets, Left vis:%s\n", m.vis ? "on" : "OFF");
				} else if (!m.object) {
					pb_print("Model not loaded (archive on the disc?)\n");
				} else {
					const char *lod_mesh = "";
					if (m.object->Class_ID() == RenderObjClass::CLASSID_HLOD) {
						RenderObjClass *current = static_cast<HLodClass *>(m.object)->Get_Current_LOD();
						if (current) {
							lod_mesh = current->Get_Name();
							current->Release_Ref();
						}
					}
					Vector3 to_model = m.sphere.Center - m.fly.eye;
					pb_print("%dK LOD %d/%d %.18s %dpoly d%d\n", m.memory_kb, m.object->Get_LOD_Level(),
					         m.object->Get_LOD_Count(), lod_mesh, m.object->Get_Num_Polys(), (int)to_model.Length());
					pb_print("Sticks fly, triggers up/down, Back resets\n");
					if (model_index == MODEL_TERRAIN) {
						pb_print("Interiors %d/%d %dK %s (White)\n", TerrainExtrasLoaded, (int)TERRAIN_EXTRAS,
						         TerrainExtrasKB, TerrainExtrasShown ? "shown" : "hidden");
					}
					if (model_index == MODEL_HAVOC) {
						const AnimEntry &a = Anims[anim_index];
						pb_print("Anim %d/%d %.18s %.16s\n", anim_index + 1, (int)ANIM_COUNT, a.name + 10, a.label);
						if (a.anim) {
							pb_print("frame %d/%d at %dfps  %dK%s\n", (int)anim_frame, a.anim->Get_Num_Frames(),
							         (int)a.anim->Get_Frame_Rate(), a.memory_kb, blend < 1.0f ? "  blending" : "");
						} else {
							pb_print("NOT FOUND in the archives (base pose shown)\n");
						}
						pb_print("%d/%d found %dK  L/R anim  Up blend:%s\n", AnimsFound, (int)ANIM_COUNT, AnimsKB,
						         blending ? "on" : "off");
					}
				}
				/* What the Direct3D layer met but doesn't support yet (at most 3 lines). */
				for (int i = 0; i < 3 && XboxD3D_Get_Notice(i); i++) {
					const char *n = XboxD3D_Get_Notice(i);
					if (!strncmp(n, "texture: ", 9)) n += 9;
					pb_print("! %.56s\n", n);
				}
			}
			if (w_one) pb_print("DIAGNOSTIC w=1: no perspective correction\n");
			pb_print("A churn B cull Y scene X base St w1 Blk txt St+Bk menu%s\n", Pad ? "" : "!");
		}
		pb_draw_text_screen();

		if (frame < 2) debugPrint("  frame %u: WW3D::End_Render\n", frame);
		if (WW3D::End_Render(true) != WW3D_ERROR_OK) fail("WW3D::End_Render", true);
		if (level_view) static_cast<PhysicsSceneClass *>(level_view->scene)->Post_Render_Processing();
		if (frame == 1) XboxPort_Trace = NULL;   /* markers only for the first frames */

		/* For the first 2 seconds keep the text screen up while frames render behind it
		** (pbkit only shows finished frames while its debug screen is off), so the markers stay
		** readable and a stalled loop is obvious. Then hand the screen to the GPU's frames. */
		if (frame < 120) {
			pb_show_debug_screen();
			if (frame % 30 == 29) debugPrint("  frames rendered: %u\n", frame + 1);
		} else if (frame == 120) {
			debugPrint("  switching the screen to the rendered frames now\n");
			pb_show_front_screen();
		}
	}
	return 0;
}
