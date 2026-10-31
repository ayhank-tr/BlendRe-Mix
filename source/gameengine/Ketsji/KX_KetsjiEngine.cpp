/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 * The engine ties all game modules together. 
 */

/** \file gameengine/Ketsji/KX_KetsjiEngine.cpp
 *  \ingroup ketsji
 */

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#include <iostream>
#include <stdio.h>

#include "BLI_task.h"

#include "KX_KetsjiEngine.h"

#include <algorithm>
#include <cmath>
#include "KX_FramePacing.h"

#include "EXP_ListValue.h"
#include "EXP_IntValue.h"
#include "EXP_VectorValue.h"
#include "EXP_BoolValue.h"
#include "EXP_FloatValue.h"

#include "RAS_BucketManager.h"
#include "RAS_Rect.h"
#include "RAS_IRasterizer.h"
#include "RAS_ICanvas.h"
#include "RAS_ILightObject.h"
#include "RAS_Deformer.h"
#include "MT_Vector3.h"
#include "MT_Transform.h"
#include "SCA_IInputDevice.h"
#include "KX_Camera.h"
#include "KX_Dome.h"
#include "KX_Light.h"
#include "KX_PythonInit.h"
#include "KX_PyConstraintBinding.h"
#include "PHY_IPhysicsEnvironment.h"

#include "NG_NetworkScene.h"
#include "NG_NetworkDeviceInterface.h"

#include "KX_WorldInfo.h"
#include "KX_ISceneConverter.h"
#include "KX_TimeCategoryLogger.h"

#include "RAS_FramingManager.h"
#include "DNA_world_types.h"
#include "DNA_lamp_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "KX_NavMeshObject.h"

#include "BL_Action.h" // For managing action lock.
#include "GPU_material.h" /* BGE-Remix P8.5 */
#include "GPU_glew.h"
#include "GPU_glcheck.h" /* BGE-Remix P8.1 */
#include "GPU_layer.h"   /* BGE-Remix P8.2 */
#include "GPU_probes.h"  /* BGE-Remix P8 Part B */
#include "GPU_volumes.h"
extern "C" {
#include "BKE_scene.h"  /* BGE-Remix P8 Part B: the pipeline */
#include "BKE_idprop.h"
#include "BKE_material.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "DNA_material_types.h"
#include "DNA_texture_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_smoke_types.h"
#include "DNA_object_force.h"
#include "BKE_object.h"
#include "BKE_modifier.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BLI_math.h"
#ifdef WITH_SMOKE
#  include "BKE_smoke.h"
#  include "smoke_API.h"
#endif
}
#include <algorithm>
#include <map>

#ifdef WITH_REMIX
#  include "KX_RemixRenderer.h"
#endif

#define DEFAULT_LOGIC_TIC_RATE 60.0
//#define DEFAULT_PHYSICS_TIC_RATE 60.0

const char KX_KetsjiEngine::m_profileLabels[tc_numCategories][15] = {
	"Physics:",		// tc_physics
	"Logic:",		// tc_logic
	"Animations:",	// tc_animations
	"Network:",		// tc_network
	"Scenegraph:",	// tc_scenegraph
	"Rasterizer:",	// tc_rasterizer
	"Services:",	// tc_services
	"Overhead:",	// tc_overhead
	"Outside:",		// tc_outside
	"GPU Latency:"	// tc_latency
};

double KX_KetsjiEngine::m_ticrate = DEFAULT_LOGIC_TIC_RATE;
int	   KX_KetsjiEngine::m_maxLogicFrame = 5;
int	   KX_KetsjiEngine::m_maxPhysicsFrame = 5;
double KX_KetsjiEngine::m_anim_framerate = 25.0;
double KX_KetsjiEngine::m_suspendedtime = 0.0;
double KX_KetsjiEngine::m_suspendeddelta = 0.0;
double KX_KetsjiEngine::m_average_framerate = 0.0;
bool   KX_KetsjiEngine::m_restrict_anim_fps = false;
short  KX_KetsjiEngine::m_exitkey = 130; // ESC Key
bool   KX_KetsjiEngine::m_doRender = true;

/**
 *	Constructor of the Ketsji Engine
 */
KX_KetsjiEngine::KX_KetsjiEngine(KX_ISystem* system)
	:	m_canvas(NULL),
	m_rasterizer(NULL),
	m_kxsystem(system),
	m_sceneconverter(NULL),
	m_networkdevice(NULL),
#ifdef WITH_PYTHON
	m_pythondictionary(NULL),
#endif
	m_keyboarddevice(NULL),
	m_mousedevice(NULL),

	m_bInitialized(false),
	m_activecam(0),
	m_bFixedTime(false),
	m_useExternalClock(false),
	
	m_firstframe(true),
	
	m_frameTime(0.f),
	m_clockTime(0.f),
	m_previousClockTime(0.f),
	m_previousAnimTime(0.f),
	m_timescale(1.0f),
	m_previousRealTime(0.0f),


	m_exitcode(KX_EXIT_REQUEST_NO_REQUEST),
	m_gl_check(NULL),
	m_exitstring(""),

	m_cameraZoom(1.0f),
	
	m_overrideCam(false),
	m_overrideCamUseOrtho(false),
	m_overrideCamNear(0.0f),
	m_overrideCamFar(0.0f),
	m_overrideCamZoom(1.0f),

	m_stereo(false),
	m_curreye(0),

	m_logger(NULL),
	
	// Set up timing info display variables
	m_show_framerate(false),
	m_show_profile(false),
	m_showProperties(false),
	m_showBackground(false),
	m_show_debug_properties(false),
	m_autoAddDebugProperties(true),

	m_animation_record(false),

	// Default behavior is to hide the cursor every frame.
	m_hideCursor(false),

	m_overrideFrameColor(false),
	m_overrideFrameColorR(0.0f),
	m_overrideFrameColorG(0.0f),
	m_overrideFrameColorB(0.0f),
	m_overrideFrameColorA(0.0f),

	m_usedome(false)
{
	m_remix = NULL;
	m_remixScene = NULL;
	m_remixOverride = -1;
	m_remixFastOverride = -1;
	m_remixWindowModeAllowed = false;
	m_probes_scene = NULL;  /* BGE-Remix P8 Part B (P8.18) */
	m_probes_scan = 0;
	m_mirror_drawing = false;

	// Initialize the time logger
	m_logger = new KX_TimeCategoryLogger (25);

	for (int i = tc_first; i < tc_numCategories; i++)
		m_logger->AddCategory((KX_TimeCategory)i);

#ifdef WITH_PYTHON
	m_pyprofiledict = PyDict_New();
#endif

	m_taskscheduler = BLI_task_scheduler_create(TASK_SCHEDULER_AUTO_THREADS);

	BL_Action::InitLock();
}



/**
 *	Destructor of the Ketsji Engine, release all memory
 */
KX_KetsjiEngine::~KX_KetsjiEngine()
{
#ifdef WITH_REMIX
	delete m_remix;
	m_remix = NULL;
#endif
	delete m_logger;
	if (m_usedome)
		delete m_dome;
	delete m_gl_check;  /* BGE-Remix P8.1 */

#ifdef WITH_PYTHON
	Py_CLEAR(m_pyprofiledict);
#endif

	if (m_taskscheduler)
		BLI_task_scheduler_free(m_taskscheduler);

	BL_Action::EndLock();
}



void KX_KetsjiEngine::SetKeyboardDevice(SCA_IInputDevice* keyboarddevice)
{
	MT_assert(keyboarddevice);
	m_keyboarddevice = keyboarddevice;
}



void KX_KetsjiEngine::SetMouseDevice(SCA_IInputDevice* mousedevice)
{
	MT_assert(mousedevice);
	m_mousedevice = mousedevice;
}



void KX_KetsjiEngine::SetNetworkDevice(NG_NetworkDeviceInterface* networkdevice)
{
	MT_assert(networkdevice);
	m_networkdevice = networkdevice;
}


void KX_KetsjiEngine::SetCanvas(RAS_ICanvas* canvas)
{
	MT_assert(canvas);
	m_canvas = canvas;
}



void KX_KetsjiEngine::SetRasterizer(RAS_IRasterizer* rasterizer)
{
	MT_assert(rasterizer);
	m_rasterizer = rasterizer;
}

#ifdef WITH_PYTHON
/*
 * At the moment the bge.logic module is imported into 'pythondictionary' after this function is called.
 * if this function ever changes to assign a copy, make sure the game logic module is imported into this dictionary before hand.
 */
void KX_KetsjiEngine::SetPyNamespace(PyObject *pythondictionary)
{
	MT_assert(pythondictionary);
	m_pythondictionary = pythondictionary;
}

PyObject* KX_KetsjiEngine::GetPyProfileDict()
{
	Py_INCREF(m_pyprofiledict);
	return m_pyprofiledict;
}
#endif


void KX_KetsjiEngine::SetSceneConverter(KX_ISceneConverter* sceneconverter)
{
	MT_assert(sceneconverter);
	m_sceneconverter = sceneconverter;
}

void KX_KetsjiEngine::InitDome(short res, short mode, short angle, float resbuf, short tilt, struct Text* text)
{
	m_dome = new KX_Dome(m_canvas, m_rasterizer,this, res, mode, angle, resbuf, tilt, text);
	m_usedome = true;
}

void KX_KetsjiEngine::RenderDome()
{
	const GLint *viewport = m_canvas->GetViewPort();
	
	m_dome->SetViewPort(viewport);

	KX_Scene* firstscene = *m_scenes.begin();
	const RAS_FrameSettings &framesettings = firstscene->GetFramingType();

	m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);

	// hiding mouse cursor each frame
	// (came back when going out of focus and then back in again)
	if (m_hideCursor)
		m_canvas->SetMouseState(RAS_ICanvas::MOUSE_INVISIBLE);

	// clear the entire game screen with the border color
	// only once per frame

	m_canvas->BeginDraw();

	// BeginFrame() sets the actual drawing area. You can use a part of the window
	if (!BeginFrame())
		return;

	KX_SceneList::iterator sceneit;
	KX_Scene* scene = NULL;

	int n_renders=m_dome->GetNumberRenders(); // usually 4 or 6
	for (int i=0;i<n_renders;i++) {
		m_canvas->ClearBuffer(RAS_ICanvas::COLOR_BUFFER|RAS_ICanvas::DEPTH_BUFFER);
		for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); sceneit++)
		// for each scene, call the proceed functions
		{
			scene = *sceneit;
			KX_SetActiveScene(scene);
			KX_Camera* cam = scene->GetActiveCamera();

			// pass the scene's worldsettings to the rasterizer
			scene->GetWorldInfo()->UpdateWorldSettings();

			// shadow buffers
			if (i == 0) {
				GPU_pass_timer_begin(GPU_PASS_SHADOWS);
				RenderShadowBuffers(scene);
				GPU_pass_timer_end(GPU_PASS_SHADOWS);
				/* BGE-Remix P8 Part B (P8.18): reflection probes */
				if (sceneit == m_scenes.begin())
					RenderProbes(scene);
			}
			/* BGE-Remix P8 Part B (P8.12): the scene's pipeline target */
			m_rasterizer->BeginScene(scene->GetBlenderScene(), sceneit == m_scenes.begin(),
			                         scene->GetBlenderScene()->gm.aasamples);
			// Avoid drawing the scene with the active camera twice when its viewport is enabled
			if (cam && !cam->GetViewport())
			{
				if (scene->IsClearingZBuffer())
					m_rasterizer->ClearDepthBuffer();
		
				m_rasterizer->SetAuxilaryClientInfo(scene);
		
				// do the rendering
				m_dome->RenderDomeFrame(scene,cam, i);
				// render all the font objects for this scene
				scene->RenderFonts();
			}
			
			list<class KX_Camera*>* cameras = scene->GetCameras();
			
			// Draw the scene once for each camera with an enabled viewport
			list<KX_Camera*>::iterator it = cameras->begin();
			while (it != cameras->end()) {
				if ((*it)->GetViewport())
				{
					if (scene->IsClearingZBuffer())
						m_rasterizer->ClearDepthBuffer();
			
					m_rasterizer->SetAuxilaryClientInfo(scene);
			
					// do the rendering
					m_dome->RenderDomeFrame(scene, (*it),i);
					// render all the font objects for this scene
					scene->RenderFonts();
				}
				
				it++;
			}
			m_rasterizer->EndScene();
			// Part of PostRenderScene()
			m_rasterizer->MotionBlur();
			scene->Render2DFilters(m_canvas);
			// no RunDrawingCallBacks
			// no FlushDebugLines
		}
		m_dome->BindImages(i);
	}

	m_canvas->EndFrame();//XXX do we really need that?

	m_canvas->SetViewPort(0, 0, m_canvas->GetWidth(), m_canvas->GetHeight());

	if (m_overrideFrameColor) //XXX why do we want
	{
		// Do not use the framing bar color set in the Blender scenes
		m_canvas->ClearColor(
			m_overrideFrameColorR,
			m_overrideFrameColorG,
			m_overrideFrameColorB,
			m_overrideFrameColorA
			);
	}
	else
	{
		// Use the framing bar color set in the Blender scenes
		m_canvas->ClearColor(
			framesettings.BarRed(),
			framesettings.BarGreen(),
			framesettings.BarBlue(),
			1.0
			);
	}
	m_dome->Draw();

	// Draw Callback for the last scene
#ifdef WITH_PYTHON
	PHY_SetActiveEnvironment(scene->GetPhysicsEnvironment());
	m_rasterizer->SyncOldMatrices();  /* BGE-Remix P8 Part B: for Python's bgl */
	scene->RunDrawingCallbacks(scene->GetPostDrawCB());
#endif
	EndFrame();
}

/**
 * Ketsji Init(), Initializes data-structures and converts data from
 * Blender into Ketsji native (realtime) format also sets up the
 * graphics context
 */
void KX_KetsjiEngine::StartEngine(bool clearIpo)
{
	m_clockTime = m_kxsystem->GetTimeInSeconds();
	m_frameTime = m_kxsystem->GetTimeInSeconds();
	m_previousClockTime = m_kxsystem->GetTimeInSeconds();
	m_previousRealTime = m_kxsystem->GetTimeInSeconds();

	m_firstframe = true;
	m_bInitialized = true;

#ifdef WITH_REMIX
	/* Remix itself starts only when a scene asks for it */
	if (m_remix == NULL) {
		m_remix = new KX_RemixRenderer(m_canvas, m_remixWindowModeAllowed);
	}
#endif
	// there is always one scene enabled at startup
	Scene* scene = m_scenes[0]->GetBlenderScene();
	if (scene)
	{
		m_ticrate = scene->gm.ticrate ? scene->gm.ticrate : DEFAULT_LOGIC_TIC_RATE;
		m_maxLogicFrame = scene->gm.maxlogicstep ? scene->gm.maxlogicstep : 5;
		m_maxPhysicsFrame = scene->gm.maxphystep ? scene->gm.maxlogicstep : 5;
	}
	else
	{
		m_ticrate = DEFAULT_LOGIC_TIC_RATE;
		m_maxLogicFrame = 5;
		m_maxPhysicsFrame = 5;
	}
	
	if (m_animation_record)
	{
		m_sceneconverter->ResetPhysicsObjectsAnimationIpo(clearIpo);
		m_sceneconverter->WritePhysicsObjectToAnimationIpo(m_currentFrame);
	}
}

void KX_KetsjiEngine::ClearFrame()
{
	/* BGE-Remix P8 Part B: interlaced stereo's right eye is cleared too (the
	 * left eye's picture is kept aside, FinishStereo puts its rows back) */

	// clear the viewports with the background color of the first scene
	bool doclear = false;
	KX_SceneList::iterator sceneit;
	RAS_Rect clearvp, area, viewport;

	for (sceneit = m_scenes.begin(); sceneit != m_scenes.end(); sceneit++)
	{
		KX_Scene* scene = *sceneit;
		//const RAS_FrameSettings &framesettings = scene->GetFramingType();
		list<class KX_Camera*>* cameras = scene->GetCameras();

		list<KX_Camera*>::iterator it;
		for (it = cameras->begin(); it != cameras->end(); it++)
		{
			GetSceneViewport(scene, (*it), area, viewport);

			if (!doclear) {
				clearvp = viewport;
				doclear = true;
			}
			else {
				if (viewport.GetLeft() < clearvp.GetLeft())
					clearvp.SetLeft(viewport.GetLeft());
				if (viewport.GetBottom() < clearvp.GetBottom())
					clearvp.SetBottom(viewport.GetBottom());
				if (viewport.GetRight() > clearvp.GetRight())
					clearvp.SetRight(viewport.GetRight());
				if (viewport.GetTop() > clearvp.GetTop())
					clearvp.SetTop(viewport.GetTop());

			}
		}
	}

	if (doclear) {
		KX_Scene* firstscene = *m_scenes.begin();
		firstscene->GetWorldInfo()->UpdateBackGround();

		m_canvas->SetViewPort(clearvp.GetLeft(), clearvp.GetBottom(),
			clearvp.GetRight(), clearvp.GetTop());
		m_rasterizer->ClearColorBuffer();
	}
}

bool KX_KetsjiEngine::BeginFrame()
{
	// set the area used for rendering (stereo can assign only a subset)
	m_rasterizer->SetRenderArea();

	if (m_canvas->BeginDraw())
	{
		ClearFrame();

		m_rasterizer->BeginFrame(m_kxsystem->GetTimeInSeconds());

		return true;
	}
	
	return false;
}


void KX_KetsjiEngine::EndFrame()
{
	/* BGE-Remix P8 Part B: interlaced stereo's left eye, before the blur */
	if (m_rasterizer->Stereo())
		m_rasterizer->FinishStereo();
	m_rasterizer->MotionBlur();

	// Show profiling info
	m_logger->StartLog(tc_overhead, m_kxsystem->GetTimeInSeconds(), true);
#ifdef WITH_REMIX
	if (m_remix) {
		const char *notice = m_remix->GetNotice(m_kxsystem->GetTimeInSeconds());
		if (notice)
			RenderRemixNotice(notice);
	}
#endif
	if (m_show_framerate || m_show_profile || (m_show_debug_properties))
	{
		GPU_pass_timer_begin(GPU_PASS_OVERLAYS);
		RenderDebugProperties();
		GPU_pass_timer_end(GPU_PASS_OVERLAYS);
	}

	double tottime = m_logger->GetAverage();
	if (tottime < 1e-6)
		tottime = 1e-6;

#ifdef WITH_PYTHON
	for (int i = tc_first; i < tc_numCategories; ++i) {
		double time = m_logger->GetAverage((KX_TimeCategory)i);
		PyObject *val = PyTuple_New(2);
		PyTuple_SetItem(val, 0, PyFloat_FromDouble(time*1000.0));
		PyTuple_SetItem(val, 1, PyFloat_FromDouble(time/tottime * 100.0));

		PyDict_SetItemString(m_pyprofiledict, m_profileLabels[i], val);
		Py_DECREF(val);
	}
	/* BGE-Remix P8 Part B (P8.11): instanced draws and the objects they drew */
	{
		int draws = 0, objects = 0;
		m_rasterizer->GetInstancingStats(&draws, &objects);
		PyObject *val = PyTuple_New(2);
		PyTuple_SetItem(val, 0, PyFloat_FromDouble((double)draws));
		PyTuple_SetItem(val, 1, PyFloat_FromDouble((double)objects));
		PyDict_SetItemString(m_pyprofiledict, "Instancing", val);
		Py_DECREF(val);
	}
	/* BGE-Remix P8 Part B (P8.11): instances the GPU culling pass tested and
	 * drew (-1: not read back, see BGE_REMIX_DRAW_STATS) */
	{
		int tested = 0, drawn = -1;
		m_rasterizer->GetCullingStats(&tested, &drawn);
		PyObject *val = PyTuple_New(2);
		PyTuple_SetItem(val, 0, PyFloat_FromDouble((double)tested));
		PyTuple_SetItem(val, 1, PyFloat_FromDouble((double)drawn));
		PyDict_SetItemString(m_pyprofiledict, "Culling", val);
		Py_DECREF(val);
	}
	/* BGE-Remix P8 Part B: the GPU time of each pass ("GPU Shadows"...) */
	for (int i = 0; i < GPU_PASS_COUNT; i++) {
		const float ms = GPU_pass_timer_ms((GPUPassTimer)i);
		PyObject *val = PyTuple_New(2);
		PyTuple_SetItem(val, 0, PyFloat_FromDouble(ms));
		PyTuple_SetItem(val, 1, PyFloat_FromDouble(ms >= 0.0f ? ms / (tottime * 1000.0) * 100.0 : 0.0));
		char key[64];
		snprintf(key, sizeof(key), "GPU %s", GPU_pass_timer_name((GPUPassTimer)i));
		PyDict_SetItemString(m_pyprofiledict, key, val);
		Py_DECREF(val);
	}
#endif

	m_average_framerate = 1.0/tottime;

	// Go to next profiling measurement, time spent after this call is shown in the next frame.
	m_logger->NextMeasurement(m_kxsystem->GetTimeInSeconds());
#ifdef WITH_REMIX
	if (m_remix) {
		/* BGE-Remix (P4.8): the frame's time split for the RTX tests */
		m_remix->AddProfileSample(m_logger->GetLast(tc_logic), m_logger->GetLast(tc_physics),
		                          m_logger->GetLast(tc_animations), m_logger->GetLast(tc_scenegraph));
	}
#endif

	m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
	m_rasterizer->EndFrame();
	// swap backbuffer (drawing into this buffer) <-> front/visible buffer
	m_logger->StartLog(tc_latency, m_kxsystem->GetTimeInSeconds(), true);
	m_rasterizer->SwapBuffers();
	m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
	
	m_canvas->EndDraw();

	/* BGE-Remix P8.2: the streaming buffer's frame */
	GPU_stream_frame_end();
	/* BGE-Remix P8 Part B: the GPU pass times */
	GPU_pass_timer_frame();
}

//#include "PIL_time.h"
//#include "LinearMath/btQuickprof.h"


/* BGE-Remix (CPU performance, from BGE NEXT): with the frame rate limited, the next tic is due
 * when the clock reaches m_frameTime + timestep. The clock only moves with real time here, so
 * sleeping until just before then changes nothing the game sees. */
void KX_KetsjiEngine::PaceUntilNextTic(double timestep)
{
	if (m_bFixedTime || m_useExternalClock || m_timescale <= 0.0) {
		return;
	}
	const double wait = (m_frameTime + timestep - m_clockTime) / m_timescale;
	if (wait > 0.0) {
		KX_PaceSleep(wait);
	}
}

bool KX_KetsjiEngine::NextFrame()
{
	double timestep =  m_timescale / m_ticrate;
	double framestep = timestep;
	//	static hidden::Clock sClock;

	m_logger->StartLog(tc_services, m_kxsystem->GetTimeInSeconds(),true);

	//float dt = sClock.getTimeMicroseconds() * 0.000001f;
	//sClock.reset();

	/*
	 * Clock advancement. There is basically three case:
	 *   - m_useExternalClock is true, the user is responsible to advance the time
	 *   manually using setClockTime, so here, we do not do anything.
	 *   - m_useExternalClock is false, m_bFixedTime is true, we advance for one
	 *   timestep, which already handle the time scaling parameter
	 *   - m_useExternalClock is false, m_bFixedTime is false, we consider how much
	 *   time has elapsed since last call and we scale this time by the time
	 *   scaling parameter. If m_timescale is 1.0 (default value), the clock
	 *   corresponds to the computer clock.
	 *
	 * Once clockTime has been computed, we will compute how many logic frames
	 * will be executed before the next rendering phase (which will occur at "clockTime").
	 * The game time elapsing between two logic frames (called framestep)
	 * depends on several variables:
	 *   - ticrate 
	 *   - max_physic_frame
	 *   - max_logic_frame
	 * XXX The logic over computation framestep is definitively not clear (and
	 * I'm not even sure it is correct). If needed frame is strictly greater
	 * than max_physics_frame, we are doing a jump in game time, but keeping
	 * framestep = 1 / ticrate, while if frames is greater than
	 * max_logic_frame, we increase framestep.
	 *
	 * XXX render.fps is not considred anywhere.
	 */
	if (!m_useExternalClock) {
		if (m_bFixedTime) {
			m_clockTime += timestep;
		}
		else {
			double current_time = m_kxsystem->GetTimeInSeconds();
			double dt = current_time - m_previousRealTime;
			m_previousRealTime = current_time;
			// m_clockTime += dt;
			m_clockTime += dt * m_timescale;
		}
	}
	
	double deltatime = m_clockTime - m_frameTime;
	if (deltatime<0.0)
	{
		// We got here too quickly, which means there is nothing to do, just return and don't render.
		// Not sure if this is the best fix, but it seems to stop the jumping framerate issue (#33088)
		PaceUntilNextTic(timestep);
		return false;
	}

	// Compute the number of logic frames to do each update (fixed tic bricks)
	int frames = int(deltatime * m_ticrate / m_timescale + 1e-6);
	if (frames == 0) {
		/* BGE-Remix: no tic due yet; sleep instead of spinning until one is */
		PaceUntilNextTic(timestep);
	}
//	if (frames>1)
//		printf("****************************************");
//	printf("dt = %f, deltatime = %f, frames = %d\n",dt, deltatime,frames);
	
//	if (!frames)
//		PIL_sleep_ms(1);
	KX_SceneList::iterator sceneit;
	
	if (frames>m_maxPhysicsFrame)
	{
	
	//	printf("framedOut: %d\n",frames);
		m_frameTime+=(frames-m_maxPhysicsFrame)*timestep;
		frames = m_maxPhysicsFrame;
	}
	

	bool doRender = frames>0;

	if (frames > m_maxLogicFrame)
	{
		framestep = (frames*timestep)/m_maxLogicFrame;
		frames = m_maxLogicFrame;
	}

	while (frames)
	{
	

		m_frameTime += framestep;
		
		m_sceneconverter->MergeAsyncLoads();

		for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); ++sceneit)
		// for each scene, call the proceed functions
		{
			KX_Scene* scene = *sceneit;
	
			/* Suspension holds the physics and logic processing for an
			 * entire scene. Objects can be suspended individually, and
			 * the settings for that precede the logic and physics
			 * update. */
			m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);

			m_sceneconverter->resetNoneDynamicObjectToIpo(); // this is for none dynamic objects with ipo

			scene->UpdateObjectActivity();
	
			if (!scene->IsSuspended())
			{
				// if the scene was suspended recalculate the delta tu "curtime"
				m_suspendedtime = scene->getSuspendedTime();
				if (scene->getSuspendedTime()!=0.0)
					scene->setSuspendedDelta(scene->getSuspendedDelta()+m_clockTime-scene->getSuspendedTime());
				m_suspendeddelta = scene->getSuspendedDelta();

				
				m_logger->StartLog(tc_network, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_NETWORK);
				scene->GetNetworkScene()->proceed(m_frameTime);
	
				//m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				//SG_SetActiveStage(SG_STAGE_NETWORK_UPDATE);
				//scene->UpdateParents(m_frameTime);
				
				m_logger->StartLog(tc_physics, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_PHYSICS1);
				// set Python hooks for each scene
#ifdef WITH_PYTHON
				PHY_SetActiveEnvironment(scene->GetPhysicsEnvironment());
#endif
				KX_SetActiveScene(scene);
	
				scene->GetPhysicsEnvironment()->EndFrame();
				
				// Update scenegraph after physics step. This maps physics calculations
				// into node positions.
				//m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				//SG_SetActiveStage(SG_STAGE_PHYSICS1_UPDATE);
				//scene->UpdateParents(m_frameTime);
				
				// Process sensors, and controllers
				m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_CONTROLLER);
				scene->LogicBeginFrame(m_frameTime);
	
				// Scenegraph needs to be updated again, because Logic Controllers 
				// can affect the local matrices.
				m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_CONTROLLER_UPDATE);
				scene->UpdateParents(m_frameTime);
	
				// Process actuators
	
				// Do some cleanup work for this logic frame
				m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_ACTUATOR);
				scene->LogicUpdateFrame(m_frameTime, true);
				
				scene->LogicEndFrame();
	
				// Actuators can affect the scenegraph
				m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_ACTUATOR_UPDATE);
				scene->UpdateParents(m_frameTime);

				// update levels of detail
				scene->UpdateObjectLods();

				m_logger->StartLog(tc_physics, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_PHYSICS2);
				scene->GetPhysicsEnvironment()->BeginFrame();
		
				// Perform physics calculations on the scene. This can involve 
				// many iterations of the physics solver.
				scene->GetPhysicsEnvironment()->ProceedDeltaTime(m_frameTime,timestep,framestep);//m_deltatimerealDeltaTime);

				m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_PHYSICS2_UPDATE);
				scene->UpdateParents(m_frameTime);
			
			
				if (m_animation_record)
				{
					m_sceneconverter->WritePhysicsObjectToAnimationIpo(++m_currentFrame);
				}

				scene->setSuspendedTime(0.0);
			} // suspended
			else
				if (scene->getSuspendedTime()==0.0)
					scene->setSuspendedTime(m_clockTime);
			
			m_logger->StartLog(tc_services, m_kxsystem->GetTimeInSeconds(), true);

			// invalidates the shadow buffer from previous render/ImageRender because the scene has changed
			scene->SetShadowDone(false);
		}

		// update system devices
		m_logger->StartLog(tc_logic, m_kxsystem->GetTimeInSeconds(), true);
		if (m_keyboarddevice)
			m_keyboarddevice->NextFrame();
	
		if (m_mousedevice)
			m_mousedevice->NextFrame();
		
		if (m_networkdevice)
			m_networkdevice->NextFrame();

		// scene management
		ProcessScheduledScenes();
		
		frames--;
	}

	// Start logging time spent outside main loop
	m_logger->StartLog(tc_outside, m_kxsystem->GetTimeInSeconds(), true);
	
	return doRender && m_doRender;
}



void KX_KetsjiEngine::Render()
{
	/* BGE-Remix P8.1: a driver reset stops the game, with a message */
	if (GPU_reset_check()) {
		printf("BGE-Remix: the graphics driver was reset; the game has stopped\n");
		RequestExit(KX_EXIT_REQUEST_QUIT_GAME);
		return;
	}
	/* BGE-Remix P8.1: what a frame leaves changed (BGE_REMIX_GL_CHECK=1) */
	if (GPU_state_check_enabled()) {
		GPUStateSnapshot now;
		GPU_state_snapshot(&now);
		if (m_gl_check) {
			GPU_state_compare(m_gl_check, &now, "game frame", 0);
		}
		else {
			m_gl_check = new GPUStateSnapshot;
		}
		*m_gl_check = now;
	}

	/* BGE-Remix P8.5: where the texture coordinate objects are */
	for (KX_SceneList::iterator sit = m_scenes.begin(); sit != m_scenes.end(); ++sit) {
		(*sit)->UpdateTexcoObjects();
	}

	if (m_usedome) {
		RenderDome();
		return;
	}
	KX_Scene* firstscene = *m_scenes.begin();
	const RAS_FrameSettings &framesettings = firstscene->GetFramingType();

	m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
	SG_SetActiveStage(SG_STAGE_RENDER);

	// hiding mouse cursor each frame
	// (came back when going out of focus and then back in again)
	if (m_hideCursor)
		m_canvas->SetMouseState(RAS_ICanvas::MOUSE_INVISIBLE);

	// clear the entire game screen with the border color
	// only once per frame
	m_canvas->BeginDraw();
	if (m_rasterizer->GetDrawingMode() == RAS_IRasterizer::KX_TEXTURED) {
		m_canvas->SetViewPort(0, 0, m_canvas->GetWidth(), m_canvas->GetHeight());
		if (m_overrideFrameColor)
		{
			// Do not use the framing bar color set in the Blender scenes
			m_canvas->ClearColor(
				m_overrideFrameColorR,
				m_overrideFrameColorG,
				m_overrideFrameColorB,
				m_overrideFrameColorA
				);
		}
		else
		{
			// Use the framing bar color set in the Blender scenes
			m_canvas->ClearColor(
				framesettings.BarRed(),
				framesettings.BarGreen(),
				framesettings.BarBlue(),
				1.0
				);
		}
		// clear the -whole- viewport
		m_canvas->ClearBuffer(RAS_ICanvas::COLOR_BUFFER|RAS_ICanvas::DEPTH_BUFFER);
	}

	m_rasterizer->SetEye(RAS_IRasterizer::RAS_STEREO_LEFTEYE);

	// BeginFrame() sets the actual drawing area. You can use a part of the window
	if (!BeginFrame())
		return;

	m_remixScene = NULL;
#ifdef WITH_REMIX
	/* Solid, shaded and textured all go to Remix (the embedded player draws
	 * like the 3D view's shading, usually Solid); not wireframe or boxes */
	const int drawmode = m_rasterizer->GetDrawingMode();
	if (m_remix && drawmode >= RAS_IRasterizer::KX_SOLID && drawmode != RAS_IRasterizer::KX_SHADOW) {
		m_remixScene = m_remix->PickScene(m_scenes, m_rasterizer->Stereo(), m_usedome,
		                                  m_remixOverride, m_remixFastOverride);
	}
#endif

	KX_SceneList::iterator sceneit;
	for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); sceneit++)
	// for each scene, call the proceed functions
	{
		KX_Scene* scene = *sceneit;
		KX_Camera* cam = scene->GetActiveCamera();
		// pass the scene's worldsettings to the rasterizer
		scene->GetWorldInfo()->UpdateWorldSettings();

		// this is now done incrementally in KX_Scene::CalculateVisibleMeshes
		//scene->UpdateMeshTransformations();

		// shadow buffers (Remix computes its own shadows)
		if (scene != m_remixScene) {
			GPU_pass_timer_begin(GPU_PASS_SHADOWS);
			RenderShadowBuffers(scene);
			GPU_pass_timer_end(GPU_PASS_SHADOWS);
			/* BGE-Remix P8 Part B (P8.18): reflection probes (the first scene's) */
			if (scene == *m_scenes.begin())
				RenderProbes(scene);
		}

		/* BGE-Remix P8 Part B (P8.12): the scene's pipeline target (not for
		 * the scene Remix draws) */
		if (scene != m_remixScene)
			m_rasterizer->BeginScene(scene->GetBlenderScene(), sceneit == m_scenes.begin(),
			                         scene->GetBlenderScene()->gm.aasamples);

		// Avoid drawing the scene with the active camera twice when its viewport is enabled
		if (cam && !cam->GetViewport())
		{
			if (scene->IsClearingZBuffer())
				m_rasterizer->ClearDepthBuffer();
	
			m_rasterizer->SetAuxilaryClientInfo(scene);
	
			// do the rendering
			/* BGE-Remix P8 Part B (P8.11): the main camera's view (GPU culling) */
			m_rasterizer->SetMainView(true);
			RenderFrame(scene, cam);
			m_rasterizer->SetMainView(false);
		}
		
		list<class KX_Camera*>* cameras = scene->GetCameras();
		
		// Draw the scene once for each camera with an enabled viewport
		list<KX_Camera*>::iterator it = cameras->begin();
		while (it != cameras->end()) {
			if ((*it)->GetViewport())
			{
				if (scene->IsClearingZBuffer())
					m_rasterizer->ClearDepthBuffer();
		
				m_rasterizer->SetAuxilaryClientInfo(scene);
		
				// do the rendering
				RenderFrame(scene, (*it));
			}
			
			it++;
		}
		m_rasterizer->EndScene();
		PostRenderScene(scene);
	}

	// only one place that checks for stereo
	if (m_rasterizer->Stereo())
	{
		m_rasterizer->SetEye(RAS_IRasterizer::RAS_STEREO_RIGHTEYE);

		if (!BeginFrame())
			return;


		for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); sceneit++)
		// for each scene, call the proceed functions
		{
			KX_Scene* scene = *sceneit;
			KX_Camera* cam = scene->GetActiveCamera();

			// pass the scene's worldsettings to the rasterizer
			scene->GetWorldInfo()->UpdateWorldSettings();
		
			if (scene != m_remixScene)
				m_rasterizer->BeginScene(scene->GetBlenderScene(), sceneit == m_scenes.begin(),
				                         scene->GetBlenderScene()->gm.aasamples);

			if (scene->IsClearingZBuffer())
				m_rasterizer->ClearDepthBuffer();

			// pass the scene, for picking and raycasting (shadows)
			m_rasterizer->SetAuxilaryClientInfo(scene);

			// do the rendering
			//RenderFrame(scene);
			RenderFrame(scene, cam);

			list<class KX_Camera*>* cameras = scene->GetCameras();
	
			// Draw the scene once for each camera with an enabled viewport
			list<KX_Camera*>::iterator it = cameras->begin();
			while (it != cameras->end()) {
				if ((*it)->GetViewport())
				{
					if (scene->IsClearingZBuffer())
						m_rasterizer->ClearDepthBuffer();
			
					m_rasterizer->SetAuxilaryClientInfo(scene);
			
					// do the rendering
					RenderFrame(scene, (*it));
				}
				
				it++;
			}
			m_rasterizer->EndScene();
			PostRenderScene(scene);
		}
	} // if (m_rasterizer->Stereo())

	EndFrame();
}



void KX_KetsjiEngine::RequestExit(int exitrequestmode)
{
	m_exitcode = exitrequestmode;
}



void KX_KetsjiEngine::SetNameNextGame(const STR_String& nextgame)
{
	m_exitstring = nextgame;
}



int KX_KetsjiEngine::GetExitCode()
{
	// if a game actuator has set an exit code or if there are no scenes left
	if (!m_exitcode)
	{
		if (m_scenes.begin() == m_scenes.end())
			m_exitcode = KX_EXIT_REQUEST_NO_SCENES_LEFT;
	}
	
	// check if the window has been closed.
	if (!m_exitcode)
	{
		//if (!m_canvas->Check()) {
		//	m_exitcode = KX_EXIT_REQUEST_OUTSIDE;
		//}
	}

	return m_exitcode;
}



const STR_String& KX_KetsjiEngine::GetExitString()
{
	return m_exitstring;
}

void KX_KetsjiEngine::EnableCameraOverride(const STR_String& forscene)
{
	m_overrideCam = true;
	m_overrideSceneName = forscene;
}

void KX_KetsjiEngine::SetCameraZoom(float camzoom)
{
	m_cameraZoom = camzoom;
}

void KX_KetsjiEngine::SetCameraOverrideUseOrtho(bool useOrtho)
{
	m_overrideCamUseOrtho = useOrtho;
}

void KX_KetsjiEngine::SetCameraOverrideProjectionMatrix(const MT_CmMatrix4x4& mat)
{
	m_overrideCamProjMat = mat;
}

void KX_KetsjiEngine::SetCameraOverrideViewMatrix(const MT_CmMatrix4x4& mat)
{
	m_overrideCamViewMat = mat;
}

void KX_KetsjiEngine::SetCameraOverrideClipping(float nearfrust, float farfrust)
{
	m_overrideCamNear = nearfrust;
	m_overrideCamFar = farfrust;
}

void KX_KetsjiEngine::SetCameraOverrideLens(float lens)
{
	m_overrideCamLens = lens;
}

void KX_KetsjiEngine::SetCameraOverrideZoom(float camzoom)
{
	m_overrideCamZoom = camzoom;
}

void KX_KetsjiEngine::GetSceneViewport(KX_Scene *scene, KX_Camera* cam, RAS_Rect& area, RAS_Rect& viewport)
{
	// In this function we make sure the rasterizer settings are up-to-date.
	// We compute the viewport so that logic using this information is up-to-date.

	// Note we postpone computation of the projection matrix
	// so that we are using the latest camera position.
	if (cam->GetViewport()) {
		RAS_Rect userviewport;

		userviewport.SetLeft(cam->GetViewportLeft()); 
		userviewport.SetBottom(cam->GetViewportBottom());
		userviewport.SetRight(cam->GetViewportRight());
		userviewport.SetTop(cam->GetViewportTop());

		// Don't do bars on user specified viewport
		RAS_FrameSettings settings = scene->GetFramingType();
		if (settings.FrameType() == RAS_FrameSettings::e_frame_bars)
			settings.SetFrameType(RAS_FrameSettings::e_frame_extend);

		RAS_FramingManager::ComputeViewport(
			scene->GetFramingType(),
			userviewport,
			viewport
		);

		area = userviewport;
	}
	else if ( !m_overrideCam || (scene->GetName() != m_overrideSceneName) ||  m_overrideCamUseOrtho ) {
		RAS_FramingManager::ComputeViewport(
			scene->GetFramingType(),
			m_canvas->GetDisplayArea(),
			viewport
		);

		area = m_canvas->GetDisplayArea();
	} else {
		viewport.SetLeft(0); 
		viewport.SetBottom(0);
		viewport.SetRight(int(m_canvas->GetWidth()));
		viewport.SetTop(int(m_canvas->GetHeight()));

		area = m_canvas->GetDisplayArea();
	}
}

void KX_KetsjiEngine::UpdateAnimations(KX_Scene *scene)
{
	if (scene->IsSuspended()) {
		return;
	}

	// Handle the animations independently of the logic time step
	if (GetRestrictAnimationFPS()) {
		double anim_timestep = 1.0 / KX_GetActiveScene()->GetAnimationFPS();
		if (m_frameTime - m_previousAnimTime > anim_timestep || m_frameTime == m_previousAnimTime) {
			// Sanity/debug print to make sure we're actually going at the fps we want (should be close to anim_timestep)
			// printf("Anim fps: %f\n", 1.0/(m_frameTime - m_previousAnimTime));
			m_previousAnimTime = m_frameTime;
			for (KX_SceneList::iterator sceneit = m_scenes.begin(); sceneit != m_scenes.end(); ++sceneit)
				(*sceneit)->UpdateAnimations(m_frameTime);
		}
	}
	else
		scene->UpdateAnimations(m_frameTime);
}

/* BGE-Remix P8.10: what a lamp's shadow depends on besides its own views:
 * the casters that can reach it (where they are, whether they are shown, a
 * deformer that moves them), so a lamp whose shadow would come out the same
 * is not drawn again */
static unsigned int shadow_casters_signature(
        KX_Scene *scene, KX_LightObject *light, RAS_ILightObject *raslight, int layer, unsigned int frame)
{
	unsigned int h = 2166136261u;
#define HASH_BYTES(p, n) \
	{ const unsigned char *b_ = (const unsigned char *)(p); \
	  for (size_t i_ = 0; i_ < (size_t)(n); i_++) { h ^= b_[i_]; h *= 16777619u; } } (void)0

	const bool sun = raslight->m_type == RAS_ILightObject::LIGHT_SUN;
	const MT_Vector3 lamppos = light->NodeGetWorldPosition();
	const MT_Scalar reach = raslight->m_shadowclipend;
	CListValue *objects = scene->GetObjectList();

	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *ob = (KX_GameObject *)objects->GetValue(i);
		if (ob->GetMeshCount() == 0 || !ob->GetVisible() || !ob->GetSGNode())
			continue;
		if (layer != 0 && !(ob->GetLayer() & layer))
			continue;
		if (!sun) {
			/* the object's bounding sphere, as KX_Scene's culling has it */
			const MT_Vector3 scale = ob->GetSGNode()->GetWorldScaling();
			const MT_Scalar radius = fabs(scale[scale.closestAxis()] * ob->GetSGNode()->Radius());
			if ((ob->NodeGetWorldPosition() - lamppos).length() - radius > reach)
				continue;
		}
		HASH_BYTES(&ob, sizeof(ob));
		HASH_BYTES(ob->GetOpenGLMatrix(), sizeof(float) * 16);
		RAS_Deformer *deformer = ob->GetDeformer();
		if (deformer && deformer->IsDynamic())
			HASH_BYTES(&frame, sizeof(frame));
	}
#undef HASH_BYTES
	return h;
}

void KX_KetsjiEngine::RenderShadowBuffers(KX_Scene *scene)
{
	CListValue *lightlist = scene->GetLightList();
	int i, drawmode;
	static unsigned int frame = 0;
	static const bool trace = getenv("BGE_REMIX_TRACE_SHADOWS") != NULL;

	frame++;
	m_rasterizer->SetAuxilaryClientInfo(scene);

	/* BGE-Remix P8.10: the camera the sun's cascades follow */
	KX_Camera *viewcam = scene->GetActiveCamera();
	float camtoworld[16], camwinmat[16];
	bool have_cam = false;
	if (viewcam && viewcam->hasValidProjectionMatrix()) {
		viewcam->GetCameraToWorld().getValue(camtoworld);
		viewcam->GetProjectionMatrix().getValue(camwinmat);
		have_cam = true;
	}

	for (i=0; i<lightlist->GetCount(); i++) {
		KX_GameObject *gameobj = (KX_GameObject*)lightlist->GetValue(i);

		KX_LightObject *light = (KX_LightObject*)gameobj;
		RAS_ILightObject *raslight = light->GetLightData();

		raslight->Update();

		if (light->GetVisible() && m_rasterizer->GetDrawingMode() == RAS_IRasterizer::KX_TEXTURED &&
			raslight->HasShadowBuffer())
		{
			/* BGE-Remix P8.10: a lamp whose views and casters did not change
			 * keeps its shadow */
			raslight->SetShadowCamera(have_cam ? camtoworld : NULL, have_cam ? camwinmat : NULL);
			const int layer = raslight->GetShadowLayer();
			const unsigned int sig = raslight->ShadowSignature() ^
			                         (shadow_casters_signature(scene, light, raslight, layer, frame) * 31u);
			if (raslight->m_shadowvalid && raslight->m_shadowsig == sig)
				continue;
			raslight->m_shadowsig = sig;
			raslight->m_shadowvalid = true;

			const int views = raslight->ShadowViewCount();
			if (trace)
				printf("BGE-Remix shadow: %s drawn (%d views)\n", light->GetName().ReadPtr(), views);

			/* make temporary camera */
			RAS_CameraData camdata = RAS_CameraData();
			KX_Camera *cam = new KX_Camera(scene, scene->m_callbacks, camdata, true, true);
			cam->SetName("__shadow__cam__");

			MT_Transform camtrans;

			/* switch drawmode for speed */
			drawmode = m_rasterizer->GetDrawingMode();
			m_rasterizer->SetDrawingMode(RAS_IRasterizer::KX_SHADOW);

			/* BGE-Remix P8 Part B (P8.16): every view in one pass where the
			 * card allows (the casters culled once, for all the views) */
			if (raslight->BindShadowBufferLayered(m_canvas, cam, camtrans)) {
				scene->CalculateVisibleMeshes(m_rasterizer, cam, layer);
				m_logger->StartLog(tc_animations, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_ANIMATION_UPDATE);
				UpdateAnimations(scene);
				m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
				SG_SetActiveStage(SG_STAGE_RENDER);
				scene->RenderBuckets(camtrans, m_rasterizer);
				raslight->UnbindShadowBuffer();
				m_rasterizer->SetDrawingMode(drawmode);
				cam->Release();
				continue;
			}

			/* binds framebuffer object, sets up camera .. */
			raslight->BindShadowBuffer(m_canvas, cam, camtrans);

			/* BGE-Remix P8.10: each view: a cube's faces, the sun's cascades */
			for (int view = 0; view < views; view++) {
				if (view > 0)
					raslight->SetShadowView(m_canvas, cam, camtrans, view);

				/* update scene */
				scene->CalculateVisibleMeshes(m_rasterizer, cam, layer);

				if (view == 0) {
					m_logger->StartLog(tc_animations, m_kxsystem->GetTimeInSeconds(), true);
					SG_SetActiveStage(SG_STAGE_ANIMATION_UPDATE);
					UpdateAnimations(scene);
					m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
					SG_SetActiveStage(SG_STAGE_RENDER);
				}

				/* render */
				m_rasterizer->ClearDepthBuffer();
				m_rasterizer->ClearColorBuffer();
				scene->RenderBuckets(camtrans, m_rasterizer);
			}

			/* unbind framebuffer object, restore drawmode, free camera */
			raslight->UnbindShadowBuffer();
			m_rasterizer->SetDrawingMode(drawmode);
			cam->Release();
		}
		else {
			raslight->m_shadowvalid = false;
		}
	}
	/* remember that we have a valid shadow buffer for that scene */
	scene->SetShadowDone(true);
}

/* ------------------------------------------------------------------------ */
/* BGE-Remix P8 Part B (P8.18): reflections */

static IDProperty *id_prop(ID *id, const char *name)
{
	IDProperty *props = id ? IDP_GetProperties(id, false) : NULL;
	return props ? IDP_GetPropertyFromGroup(props, name) : NULL;
}

static float id_float(ID *id, const char *name, float def)
{
	IDProperty *p = id_prop(id, name);
	if (!p)
		return def;
	if (p->type == IDP_INT)
		return (float)IDP_Int(p);
	if (p->type == IDP_FLOAT)
		return IDP_Float(p);
	if (p->type == IDP_DOUBLE)
		return (float)IDP_Double(p);
	return def;
}

void KX_KetsjiEngine::ReleaseProbes()
{
	for (size_t i = 0; i < m_probes.size(); i++) {
		GPU_probe_free(m_probes[i].slot);
		m_probes[i].obj->Release();
	}
	m_probes.clear();
	for (size_t i = 0; i < m_mirror_objects.size(); i++)
		m_mirror_objects[i].first->Release();
	m_mirror_objects.clear();
	for (size_t i = 0; i < m_volumes.size(); i++) {
		GPU_volume_grid_free(&m_volumes[i].density);
		GPU_volume_grid_free(&m_volumes[i].flame);
		m_volumes[i].obj->Release();
	}
	m_volumes.clear();
	m_probes_scene = NULL;
}

/* the probes and the mirror candidates of the scene (looked for every
 * second: objects added later are found then) */
void KX_KetsjiEngine::ScanProbes(KX_Scene *scene)
{
	if (m_probes_scene == scene && (m_probes_scan++ % 60) != 0)
		return;
	if (m_probes_scene != scene) {
		ReleaseProbes();
		m_probes_scene = scene;
		m_probes_scan = 1;
	}
	/* Static and Animated env maps: a probe at their viewpoint */
	std::map<Object *, int> envview;
	for (Material *ma = (Material *)G.main->mat.first; ma; ma = (Material *)ma->id.next) {
		for (int i = 0; i < MAX_MTEX; i++) {
			MTex *mtex = ma->mtex[i];
			EnvMap *env = (mtex && mtex->tex && mtex->tex->type == TEX_ENVMAP) ? mtex->tex->env : NULL;
			/* (a Planar one, whatever its source, is a mirror) */
			if (env && env->object && env->type != ENV_PLANE && (env->stype == ENV_STATIC || env->stype == ENV_ANIM))
				envview[env->object] = (env->stype == ENV_ANIM) ? 1 : 0;
		}
	}
	CListValue *objects = scene->GetObjectList();
	std::vector<ProbeEntry> keep;
	std::vector<bool> volume_seen(m_volumes.size(), false);
	std::vector<std::pair<KX_GameObject *, int> > mirrors;
	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		Object *ob = obj->GetBlenderObject();
		if (!ob)
			continue;
		std::map<Object *, int>::iterator env = envview.find(ob);
		const bool probe = id_float(&ob->id, "bge_probe", 0.0f) != 0.0f || env != envview.end();
		if (probe) {
			bool found = false;
			for (size_t k = 0; k < m_probes.size(); k++) {
				if (m_probes[k].obj == obj) {
					keep.push_back(m_probes[k]);
					m_probes[k].obj = NULL;
					found = true;
					break;
				}
			}
			if (!found) {
				ProbeEntry e;
				e.slot = GPU_probe_alloc();
				if (e.slot >= 0) {
					e.obj = obj;
					obj->AddRef();
					e.rate = (env != envview.end()) ? env->second : (int)id_float(&ob->id, "bge_probe_rate", 0.0f);
					e.reach = id_float(&ob->id, "bge_probe_reach", 10.0f);
					e.age = 0;
					e.drawn = false;
					keep.push_back(e);
				}
			}
		}
		/* a volume object (its material's type) */
		if (obj->GetMeshCount() > 0) {
			bool volume = false;
			for (int c = 1; c <= ob->totcol && !volume; c++) {
				Material *ma = give_current_material(ob, c);
				volume = ma && ma->material_type == MA_TYPE_VOLUME;
			}
			bool known = false;
			for (size_t k = 0; k < m_volumes.size(); k++) {
				if (m_volumes[k].obj == obj && k < volume_seen.size()) {
					known = true;
					volume_seen[k] = true;
				}
			}
			if (volume && !known) {
				VolumeEntry v;
				memset(&v, 0, sizeof(v));
				v.obj = obj;
				v.frame = -1;
				obj->AddRef();
				m_volumes.push_back(v);
			}
		}
		/* an object whose material reads a planar mirror */
		if (obj->GetMeshCount() > 0) {
			for (int c = 1; c <= ob->totcol; c++) {
				Material *ma = give_current_material(ob, c);
				if (ma && GPU_material_planar_mirror(ma)) {
					obj->AddRef();
					mirrors.push_back(std::make_pair(obj, -1));
					break;
				}
			}
		}
	}
	/* probes whose objects are gone */
	for (size_t k = 0; k < m_probes.size(); k++) {
		if (m_probes[k].obj) {
			GPU_probe_free(m_probes[k].slot);
			m_probes[k].obj->Release();
		}
	}
	m_probes.swap(keep);
	/* volumes whose objects are gone */
	for (size_t k = volume_seen.size(); k-- > 0;) {
		if (!volume_seen[k]) {
			GPU_volume_grid_free(&m_volumes[k].density);
			GPU_volume_grid_free(&m_volumes[k].flame);
			m_volumes[k].obj->Release();
			m_volumes.erase(m_volumes.begin() + k);
		}
	}
	for (size_t k = 0; k < m_mirror_objects.size(); k++)
		m_mirror_objects[k].first->Release();
	m_mirror_objects.swap(mirrors);
}

/* the scene as seen with these matrices, into the bound target (cleared) */
void KX_KetsjiEngine::RenderView(KX_Scene *scene, const MT_Matrix4x4 &view, const MT_Matrix4x4 &proj,
                                 KX_GameObject *hide, bool cull)
{
	RAS_CameraData camdata = RAS_CameraData();
	/* (a mirrored view's frustum is turned inside out: not culled) */
	KX_Camera *cam = new KX_Camera(scene, scene->m_callbacks, camdata, cull, true);
	cam->SetName("__view__cam__");
	float v[16];
	view.getValue(v);
	MT_Transform trans = MT_Transform(v);
	MT_Transform camtrans;
	camtrans.invert(trans);
	cam->SetModelviewMatrix(view);
	cam->SetProjectionMatrix(proj);
	cam->NodeSetLocalPosition(camtrans.getOrigin());
	cam->NodeSetLocalOrientation(camtrans.getBasis());
	cam->NodeUpdateGS(0);
	RAS_IRasterizer::StereoMode stereomode = m_rasterizer->GetStereoMode();
	m_rasterizer->SetStereoMode(RAS_IRasterizer::RAS_STEREO_NOSTEREO);
	m_rasterizer->SetProjectionMatrix(proj);
	m_rasterizer->SetViewMatrix(view, cam->NodeGetWorldOrientation(), cam->NodeGetWorldPosition(),
	                            cam->NodeGetLocalScaling(), true);
	m_rasterizer->SetStereoMode(stereomode);

	const bool visible = hide ? hide->GetVisible() : false;
	if (hide && visible)
		hide->SetVisible(false, false);
	scene->CalculateVisibleMeshes(m_rasterizer, cam);
	scene->GetWorldInfo()->RenderBackground(m_rasterizer);
	scene->RenderBuckets(camtrans, m_rasterizer);
	if (hide && visible)
		hide->SetVisible(true, false);
	cam->Release();
}

void KX_KetsjiEngine::RenderProbes(KX_Scene *scene)
{
	if (m_rasterizer->GetDrawingMode() != RAS_IRasterizer::KX_TEXTURED)
		return;
	ScanProbes(scene);
	/* the sky the materials reflect where no probe reaches */
	static int skyframe = 0;
	if (scene->GetWorldInfo()->DrawsSky()) {
		if ((skyframe++ % 30) == 0)
			GPU_sky_cube_update(scene->GetBlenderScene(), scene->GetBlenderScene()->world);
	}
	else {
		GPU_sky_cube_clear();
	}
	int drawn = 0;
	for (size_t i = 0; i < m_probes.size(); i++) {
		ProbeEntry &e = m_probes[i];
		const MT_Point3 &p = e.obj->NodeGetWorldPosition();
		const float center[3] = {(float)p[0], (float)p[1], (float)p[2]};
		const bool due = !e.drawn || (e.rate > 0 && ++e.age >= e.rate);
		if (!due || drawn >= 2) {
			GPU_probe_place(e.slot, center, e.reach, e.drawn);
			continue;
		}
		/* (at most two a frame) */
		e.age = 0;
		drawn++;
		GPU_probe_place(e.slot, center, e.reach, false);
		m_mirror_drawing = true;
		for (int face = 0; face < 6; face++) {
			float view[4][4], proj[4][4];
			int size;
			GPU_probe_face_begin(e.slot, face, view, proj, &size);
			RenderView(scene, MT_Matrix4x4(&view[0][0]), MT_Matrix4x4(&proj[0][0]), e.obj);
			GPU_probe_face_end();
		}
		m_mirror_drawing = false;
		GPU_probe_filter(e.slot);
		e.drawn = true;
		GPU_probe_place(e.slot, center, e.reach, true);
	}
}

/* each visible mirror (the nearest four): the camera's view mirrored in the
 * object's plane (its local Z the normal), drawn with a clip plane */
void KX_KetsjiEngine::RenderMirrors(KX_Scene *scene, KX_Camera *cam)
{
	for (size_t i = 0; i < m_mirror_objects.size(); i++)
		m_mirror_objects[i].second = -1;
	if (m_mirror_objects.empty() || m_rasterizer->GetDrawingMode() != RAS_IRasterizer::KX_TEXTURED)
		return;
	const MT_Point3 campos = cam->NodeGetWorldPosition();
	std::vector<std::pair<double, size_t> > order;
	for (size_t i = 0; i < m_mirror_objects.size(); i++) {
		KX_GameObject *obj = m_mirror_objects[i].first;
		if (obj->GetCulled() || !obj->GetVisible())
			continue;
		order.push_back(std::make_pair((double)(obj->NodeGetWorldPosition() - campos).length2(), i));
	}
	if (order.empty())
		return;
	std::sort(order.begin(), order.end());

	const MT_Matrix4x4 view = m_rasterizer->GetViewMatrix();
	const MT_Matrix4x4 proj = cam->GetProjectionMatrix();
	World *wo = scene->GetBlenderScene()->world;
	const float scale = CLAMPIS(wo ? id_float(&wo->id, "bge_mirror_scale", 0.5f) : 0.5f, 0.1f, 1.0f);
	int vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	m_mirror_drawing = true;
	for (size_t k = 0; k < order.size() && k < GPU_MIRRORS; k++) {
		KX_GameObject *obj = m_mirror_objects[order[k].second].first;
		const MT_Vector3 n = obj->NodeGetWorldOrientation().getColumn(2).safe_normalized();
		const MT_Point3 &p = obj->NodeGetWorldPosition();
		double d = -n.dot(p);
		/* the side the camera is on is kept */
		const double side = (n.dot(campos) + d) >= 0.0 ? 1.0 : -1.0;
		/* the reflection in the plane n.x + d = 0 */
		MT_Matrix4x4 R;
		R.setIdentity();
		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++)
				R[r][c] = (r == c ? 1.0 : 0.0) - 2.0 * n[r] * n[c];
			R[r][3] = -2.0 * d * n[r];
		}
		const MT_Matrix4x4 mview = view * R;
		/* the clip plane in the mirrored camera's eye space: (V'^-1)^T P */
		MT_Matrix4x4 inv = mview;
		inv.invert();
		const double P[4] = {side * n[0], side * n[1], side * n[2], side * d};
		float plane[4];
		for (int c = 0; c < 4; c++)
			plane[c] = (float)(inv[0][c] * P[0] + inv[1][c] * P[1] + inv[2][c] * P[2] + inv[3][c] * P[3]);
		GPU_mirror_begin((int)k, (int)(vp[2] * scale), (int)(vp[3] * scale), plane);
		RenderView(scene, mview, proj, obj, false);
		GPU_mirror_end((int)k);
		m_mirror_objects[order[k].second].second = (int)k;
	}
	m_mirror_drawing = false;

	/* the camera's own view again */
	m_rasterizer->SetProjectionMatrix(proj);
	m_rasterizer->SetViewMatrix(view, cam->NodeGetWorldOrientation(), cam->NodeGetWorldPosition(),
	                            cam->NodeGetLocalScaling(), cam->GetCameraData()->m_perspective);
	scene->CalculateVisibleMeshes(m_rasterizer, cam);
}

/* BGE-Remix P8 Part B (P8.19): volumes after the scene, over it */
void KX_KetsjiEngine::RenderVolumes(KX_Scene *scene)
{
	if (scene != m_probes_scene || m_volumes.empty() ||
	    m_rasterizer->GetDrawingMode() != RAS_IRasterizer::KX_TEXTURED)
	{
		return;
	}
	Scene *blscene = scene->GetBlenderScene();
	for (size_t i = 0; i < m_volumes.size(); i++) {
		VolumeEntry &e = m_volumes[i];
		KX_GameObject *obj = e.obj;
		Object *ob = obj->GetBlenderObject();
		if (!ob || !obj->GetVisible() || ob->type != OB_MESH)
			continue;
		Material *ma = NULL;
		for (int c = 1; c <= ob->totcol && !ma; c++) {
			Material *m = give_current_material(ob, c);
			if (m && m->material_type == MA_TYPE_VOLUME)
				ma = m;
		}
		if (!ma)
			continue;
		GPUVolume v;
		memset(&v, 0, sizeof(v));
		obj->GetOpenGLMatrix();
		const float *glm = obj->GetOpenGLMatrixPtr()->getPointer();
		for (int k = 0; k < 16; k++)
			(&v.obmat[0][0])[k] = glm[k];
		BoundBox *bb = BKE_object_boundbox_get(ob);
		if (bb) {
			INIT_MINMAX(v.bbmin, v.bbmax);
			for (int k = 0; k < 8; k++)
				minmax_v3v3_v3(v.bbmin, v.bbmax, bb->vec[k]);
		}
		else {
			v.bbmin[0] = v.bbmin[1] = v.bbmin[2] = -1.0f;
			v.bbmax[0] = v.bbmax[1] = v.bbmax[2] = 1.0f;
		}
		/* a mesh whose vertices all lie on the ellipsoid in its box (a
		 * sphere) is stepped inside that ellipsoid, others in their box */
		{
			const Mesh *me = (const Mesh *)ob->data;
			bool round = me && me->totvert > 8;
			for (int k = 0; round && k < me->totvert; k++) {
				float q = 0.0f;
				for (int a = 0; a < 3; a++) {
					const float half = 0.5f * (v.bbmax[a] - v.bbmin[a]);
					const float c = (me->mvert[k].co[a] - 0.5f * (v.bbmax[a] + v.bbmin[a])) / max_ff(half, 1e-6f);
					q += c * c;
				}
				round = fabsf(q - 1.0f) < 0.1f;
			}
			v.shape = round ? 1 : 0;
		}
		const VolumeSettings &vs = ma->vol;
		v.density = vs.density;
		v.density_scale = vs.density_scale;
		v.scattering = vs.scattering;
		v.emission = vs.emission;
		copy_v3_v3(v.emission_col, vs.emission_col);
		copy_v3_v3(v.transmission_col, vs.transmission_col);
		copy_v3_v3(v.reflection_col, vs.reflection_col);
		v.reflection = vs.reflection;
		v.asymmetry = vs.asymmetry;
		v.stepsize = vs.stepsize;
		v.depth_cutoff = vs.depth_cutoff;
		v.shadeless = (vs.shade_type == MA_VOL_SHADE_SHADELESS);
		v.self_shadow = (vs.shade_type == MA_VOL_SHADE_SHADED || vs.shade_type == MA_VOL_SHADE_MULTIPLE ||
		                 vs.shade_type == MA_VOL_SHADE_SHADEDPLUSMULTIPLE);
		v.ext_shadow = (vs.shade_type == MA_VOL_SHADE_SHADOWED) || (vs.shadeflag & MA_VOL_RECV_EXT_SHADOW);
		/* Classic draws sRGB colours when the scene manages them (as the
		 * GLSL materials' GPU_material_do_color_management) */
		v.srgb = BKE_scene_game_pipeline(blscene) != BKE_SCENE_GAME_PIPELINE_MODERN &&
		         BKE_scene_check_color_management_enabled(blscene) &&
		         !(blscene->gm.flag & GAME_GLSL_NO_COLOR_MANAGEMENT);

#ifdef WITH_SMOKE
		/* a baked smoke and fire simulation: the cache frame at the game's
		 * time (looping over the cache) */
		SmokeModifierData *smd = (SmokeModifierData *)modifiers_findByType(ob, eModifierType_Smoke);
		if (smd && (smd->type & MOD_SMOKE_TYPE_DOMAIN) && smd->domain && smd->domain->point_cache[0]) {
			SmokeDomainSettings *sds = smd->domain;
			PointCache *cache = sds->point_cache[0];
			const int start = cache->startframe, end = std::max(cache->endframe, cache->startframe);
			const double fps = (double)blscene->r.frs_sec / (double)std::max(blscene->r.frs_sec_base, 1e-6f);
			const int frame = start + (int)(m_clockTime * fps) % (end - start + 1);
			if (frame != e.frame) {
				e.frame = frame;
				const int cfra = blscene->r.cfra;
				blscene->r.cfra = frame;
				/* a smoke cache is on disk next to the file (blendcache_<file>):
				 * the point cache reads it only when the file's path counts as
				 * valid, which the game never says (the player loads files its
				 * own way) */
				const int relbase = G.relbase_valid;
				if (G.main && G.main->name[0])
					G.relbase_valid = 1;
				DerivedMesh *dm = CDDM_from_mesh((Mesh *)ob->data);
				DerivedMesh *res = smokeModifier_do(smd, blscene, ob, dm);
				G.relbase_valid = relbase;
				if (res && res != dm)
					res->release(res);
				dm->release(dm);
				blscene->r.cfra = cfra;
				/* (once per volume: what the cache gave, for the tests' logs) */
				static int smoke_said = 0;
				if (smoke_said < 4) {
					smoke_said++;
					if (!sds->fluid) {
						printf("BGE-Remix volume %s: smoke frame %d: nothing read from the cache (%s)\n",
						       ob->id.name + 2, frame, (cache->flag & PTCACHE_BAKED) ? "baked" : "not baked");
					}
					else {
						float mx = 0.0f;
						const float *dd = smoke_get_density(sds->fluid);
						const int n = sds->res[0] * sds->res[1] * sds->res[2];
						for (int k = 0; dd && k < n; k++)
							mx = std::max(mx, dd[k]);
						printf("BGE-Remix volume %s: smoke frame %d: grid %dx%dx%d, densest %.3f\n", ob->id.name + 2,
						       frame, sds->res[0], sds->res[1], sds->res[2], mx);
					}
				}
				if (sds->fluid) {
					const bool high = sds->wt != NULL;
					int grid[3];
					copy_v3_v3_int(grid, high ? sds->res_wt : sds->res);
					float *density = high ? smoke_turbulence_get_density(sds->wt) : smoke_get_density(sds->fluid);
					if (density && grid[0] > 0 && grid[1] > 0 && grid[2] > 0)
						GPU_volume_grid(&e.density, grid, density);
					if (!high && smoke_has_fuel(sds->fluid)) {
						float *flame = smoke_get_flame(sds->fluid);
						if (flame)
							GPU_volume_grid(&e.flame, grid, flame);
					}
					for (int k = 0; k < 3; k++) {
						e.smin[k] = sds->p0[k] + sds->cell_size[k] * sds->res_min[k];
						e.smax[k] = sds->p0[k] + sds->cell_size[k] * sds->res_max[k];
					}
				}
			}
		}
#endif
		v.smoke_density = e.density;
		v.smoke_flame = e.flame;
		copy_v3_v3(v.smoke_min, e.smin);
		copy_v3_v3(v.smoke_max, e.smax);
		GPU_volume_draw(&v);
	}
}

int KX_KetsjiEngine::MirrorIndex(KX_GameObject *obj) const
{
	if (m_mirror_drawing)
		return -1;
	for (size_t i = 0; i < m_mirror_objects.size(); i++) {
		if (m_mirror_objects[i].first == obj)
			return m_mirror_objects[i].second;
	}
	return -1;
}

/* BGE-Remix P8.10: whether a lamp of the scene has contact shadows (they read
 * the scene's depth, drawn first) */
static bool scene_needs_depth(KX_Scene *scene)
{
	CListValue *lights = scene->GetLightList();
	for (int i = 0; i < lights->GetCount(); i++) {
		KX_LightObject *light = (KX_LightObject *)lights->GetValue(i);
		RAS_ILightObject *data = light->GetLightData();
		if (data && light->GetVisible() && data->m_contact > 0.0f && data->HasShadowBuffer())
			return true;
	}
	return false;
}

// update graphics
void KX_KetsjiEngine::RenderFrame(KX_Scene* scene, KX_Camera* cam)
{
	bool override_camera;
	RAS_Rect viewport, area;
	float nearfrust, farfrust, focallength;
//	KX_Camera* cam = scene->GetActiveCamera();
	
	if (!cam)
		return;

	KX_SetActiveScene(scene);

#ifdef WITH_PYTHON
	m_rasterizer->SyncOldMatrices();  /* BGE-Remix P8 Part B: for Python's bgl */
	scene->RunDrawingCallbacks(scene->GetPreDrawSetupCB());
#endif

	GetSceneViewport(scene, cam, area, viewport);

	// store the computed viewport in the scene
	scene->SetSceneViewport(viewport);

	// set the viewport for this frame and scene
	m_canvas->SetViewPort(viewport.GetLeft(), viewport.GetBottom(),
		viewport.GetRight(), viewport.GetTop());
	
	// see KX_BlenderMaterial::Activate
	//m_rasterizer->SetAmbient();
	m_rasterizer->DisplayFog();

	override_camera = m_overrideCam && (scene->GetName() == m_overrideSceneName);
	override_camera = override_camera && (cam->GetName() == "__default__cam__");

	if (override_camera && m_overrideCamUseOrtho) {
		m_rasterizer->SetProjectionMatrix(m_overrideCamProjMat);
		if (!cam->hasValidProjectionMatrix()) {
			// needed to get frustum planes for culling
			MT_Matrix4x4 projmat;
			projmat.setValue(m_overrideCamProjMat.getPointer());
			cam->SetProjectionMatrix(projmat);
		}
	} else if (cam->hasValidProjectionMatrix())
	{
		m_rasterizer->SetProjectionMatrix(cam->GetProjectionMatrix());
	} else
	{
		RAS_FrameFrustum frustum;
		bool orthographic = !cam->GetCameraData()->m_perspective;
		nearfrust = cam->GetCameraNear();
		farfrust = cam->GetCameraFar();
		focallength = cam->GetFocalLength();
		MT_Matrix4x4 projmat;

		if (override_camera) {
			nearfrust = m_overrideCamNear;
			farfrust = m_overrideCamFar;
		}

		float camzoom = override_camera ? m_overrideCamZoom : m_cameraZoom;
		if (orthographic) {

			RAS_FramingManager::ComputeOrtho(
				scene->GetFramingType(),
				area,
				viewport,
				cam->GetScale(),
				nearfrust,
				farfrust,
				cam->GetSensorFit(),
				cam->GetShiftHorizontal(),
				cam->GetShiftVertical(),
				frustum
			);
			if (!cam->GetViewport()) {
				frustum.x1 *= camzoom;
				frustum.x2 *= camzoom;
				frustum.y1 *= camzoom;
				frustum.y2 *= camzoom;
			}
			projmat = m_rasterizer->GetOrthoMatrix(
				frustum.x1, frustum.x2, frustum.y1, frustum.y2, frustum.camnear, frustum.camfar);

		} else {
			RAS_FramingManager::ComputeFrustum(
				scene->GetFramingType(),
				area,
				viewport,
				cam->GetLens(),
				cam->GetSensorWidth(),
				cam->GetSensorHeight(),
				cam->GetSensorFit(),
				cam->GetShiftHorizontal(),
				cam->GetShiftVertical(),
				nearfrust,
				farfrust,
				frustum
			);

			if (!cam->GetViewport()) {
				frustum.x1 *= camzoom;
				frustum.x2 *= camzoom;
				frustum.y1 *= camzoom;
				frustum.y2 *= camzoom;
			}
			projmat = m_rasterizer->GetFrustumMatrix(
				frustum.x1, frustum.x2, frustum.y1, frustum.y2, frustum.camnear, frustum.camfar, focallength);
		}
		cam->SetProjectionMatrix(projmat);
		
		// Otherwise the projection matrix for each eye will be the same...
		if (!orthographic && m_rasterizer->Stereo())
			cam->InvalidateProjectionMatrix();
	}

	MT_Transform camtrans(cam->GetWorldToCamera());
	MT_Matrix4x4 viewmat(camtrans);
	
	m_rasterizer->SetViewMatrix(viewmat, cam->NodeGetWorldOrientation(), cam->NodeGetWorldPosition(), cam->NodeGetLocalScaling(), cam->GetCameraData()->m_perspective);
	cam->SetModelviewMatrix(viewmat);

	// The following actually reschedules all vertices to be
	// redrawn. There is a cache between the actual rescheduling
	// and this call though. Visibility is imparted when this call
	// runs through the individual objects.

	m_logger->StartLog(tc_scenegraph, m_kxsystem->GetTimeInSeconds(), true);
	SG_SetActiveStage(SG_STAGE_CULLING);

	scene->CalculateVisibleMeshes(m_rasterizer,cam);

#ifdef WITH_REMIX
	if (m_remix && scene == m_remixScene && !cam->GetViewport()) {
		/* BGE-Remix: Remix also shows what the camera does not see (shadows,
		 * reflections), so nothing counts as culled: every character is
		 * animated (UpdateAnimations skips culled ones). */
		CListValue *objects = scene->GetObjectList();
		for (int i = 0; i < objects->GetCount(); i++)
			((KX_GameObject *)objects->GetValue(i))->SetCulled(false);
	}
#endif

	m_logger->StartLog(tc_animations, m_kxsystem->GetTimeInSeconds(), true);
	SG_SetActiveStage(SG_STAGE_ANIMATION_UPDATE);
	UpdateAnimations(scene);

	m_logger->StartLog(tc_rasterizer, m_kxsystem->GetTimeInSeconds(), true);
	SG_SetActiveStage(SG_STAGE_RENDER);

#ifdef WITH_PYTHON
	PHY_SetActiveEnvironment(scene->GetPhysicsEnvironment());
	// Run any pre-drawing python callbacks
	m_rasterizer->SyncOldMatrices();  /* BGE-Remix P8 Part B: for Python's bgl */
	scene->RunDrawingCallbacks(scene->GetPreDrawCB());
#endif

#ifdef WITH_REMIX
	if (m_remix && scene == m_remixScene && !cam->GetViewport()) {
		/* BGE-Remix: the scene goes to Remix, with the matrices used above;
		 * the picture is drawn into the viewport. 3D text objects follow in P3. */
		MT_Matrix4x4 remixproj;
		if (override_camera && m_overrideCamUseOrtho)
			remixproj.setValue(m_overrideCamProjMat.getPointer());
		else
			remixproj = cam->GetProjectionMatrix();
		m_remix->RenderScene(scene, cam, m_rasterizer->GetViewMatrix(), remixproj, viewport);
	}
	else
#endif
	{
		/* BGE-Remix P8 Part B (P8.18): planar mirrors (the first scene's main
		 * camera), screen-space reflections (Modern scenes) */
		if (scene == *m_scenes.begin() && cam == scene->GetActiveCamera())
			RenderMirrors(scene, cam);
		World *wo = scene->GetBlenderScene()->world;
		const bool ssr = BKE_scene_game_pipeline(scene->GetBlenderScene()) == BKE_SCENE_GAME_PIPELINE_MODERN &&
		                 (!wo || id_float(&wo->id, "bge_ssr", 1.0f) != 0.0f);
		GPU_ssr_set(ssr);

		/* BGE-Remix P8.10: the scene's depth first, for contact shadows */
		const bool depth = m_rasterizer->GetDrawingMode() == RAS_IRasterizer::KX_TEXTURED &&
		                   (scene_needs_depth(scene) || ssr || HasVolumes(scene));
		if (depth) {
			const int drawmode = m_rasterizer->GetDrawingMode();
			GPU_scene_depth_begin(viewport.GetWidth() + 1, viewport.GetHeight() + 1);
			m_rasterizer->SetDrawingMode(RAS_IRasterizer::KX_SHADOW);
			scene->RenderBuckets(camtrans, m_rasterizer);
			m_rasterizer->SetDrawingMode(drawmode);
			GPU_scene_depth_end();
			/* drawing marks every mesh slot culled for the next pass */
			scene->CalculateVisibleMeshes(m_rasterizer, cam);
		}

		/* BGE-Remix P8 Part B (P8.17): the world's sky behind the first scene */
		if (scene == *m_scenes.begin())
			scene->GetWorldInfo()->RenderBackground(m_rasterizer);

		scene->RenderBuckets(camtrans, m_rasterizer);

		/* BGE-Remix P8 Part B (P8.19): volumes over the scene */
		RenderVolumes(scene);

		/* BGE-Remix P8.10: nothing else reads it (other cameras, the 3D view) */
		if (depth)
			GPU_scene_depth_disable();

		// render all the font objects for this scene
		scene->RenderFonts();

		/* BGE-Remix P8.9: spot halos as a light cone (BGE-Remix P8 Part B
		 * (P8.17): Modern scenes' fog grid draws them as light shafts) */
		if (BKE_scene_game_pipeline(scene->GetBlenderScene()) != BKE_SCENE_GAME_PIPELINE_MODERN)
			RenderSpotHalos(scene);
	}

	if (scene->GetPhysicsEnvironment())
		scene->GetPhysicsEnvironment()->DebugDrawWorld();
}

/* BGE-Remix P8.9: a spot lamp with Halo shows its cone in the game, as F12
 * shows the lit fog: a cone from the lamp to its distance, added to the
 * picture, brightest at the lamp and fading to nothing at the cone's end
 * (Blender Internal's halo is volumetric; this is its look, not its maths) */
void KX_KetsjiEngine::RenderSpotHalos(KX_Scene *scene)
{
	CListValue *lights = scene->GetLightList();
	bool started = false;
	const int segments = 32;
	GPUStateScope scope;

	for (int i = 0; i < lights->GetCount(); i++) {
		KX_LightObject *light = (KX_LightObject *)lights->GetValue(i);
		Object *ob = light->GetBlenderObject();
		Lamp *la = (ob && ob->type == OB_LAMP) ? (Lamp *)ob->data : NULL;
		RAS_ILightObject *data = light->GetLightData();
		if (!la || !data || la->type != LA_SPOT || !(la->mode & LA_HALO) || !light->GetVisible() ||
		    la->haint <= 0.0f || data->m_energy <= 0.0f)
		{
			continue;
		}
		if (!started) {
			/* BGE-Remix P8 Part B: through the layer (its matrices, the
			 * smooth shader), the state kept in a scope */
			started = true;
			GPU_state_scope_begin(&scope);
			glDisable(GL_CULL_FACE);
			GPU_state_blend(GPU_LAYER_BLEND_NONE);
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
			GPU_state_depth(true, false);
		}
		const MT_Point3 &pos = light->NodeGetWorldPosition();
		const MT_Matrix3x3 &ori = light->NodeGetWorldOrientation();
		const MT_Vector3 x = ori.getColumn(0).safe_normalized(), y = ori.getColumn(1).safe_normalized();
		const MT_Vector3 dir = -ori.getColumn(2).safe_normalized();
		const float length = std::max(data->m_distance, 0.01f);
		/* the cone's half angle (m_spotsize is the full angle, in radians) */
		const float half = std::min(data->m_spotsize, 3.12f) * 0.5f;
		const float radius = length * tanf(half);
		const float k = 0.25f * la->haint * data->m_energy;
		const float col[3] = {data->m_color[0] * k, data->m_color[1] * k, data->m_color[2] * k};

		GPU_imm_begin(GL_TRIANGLE_FAN, GPU_BUILTIN_SMOOTH);
		GPU_imm_color4f(col[0], col[1], col[2], 1.0f);
		GPU_imm_vertex3f((float)pos[0], (float)pos[1], (float)pos[2]);
		GPU_imm_color4f(0.0f, 0.0f, 0.0f, 1.0f);
		for (int s = 0; s <= segments; s++) {
			const double a = 6.283185307179586 * s / segments;
			const MT_Point3 p = pos + dir * length + (x * cos(a) + y * sin(a)) * radius;
			GPU_imm_vertex3f((float)p[0], (float)p[1], (float)p[2]);
		}
		GPU_imm_end();
	}
	if (started) {
		GPU_state_scope_end(&scope);
	}
}

/*
 * To run once per scene
 */
void KX_KetsjiEngine::PostRenderScene(KX_Scene* scene)
{
	KX_SetActiveScene(scene);

	// We need to first make sure our viewport is correct (enabling multiple viewports can mess this up)
	m_canvas->SetViewPort(0, 0, m_canvas->GetWidth(), m_canvas->GetHeight());
	
	GPU_pass_timer_begin(GPU_PASS_OVERLAYS);
	m_rasterizer->FlushDebugShapes(scene);
	GPU_pass_timer_end(GPU_PASS_OVERLAYS);
#ifdef WITH_REMIX
	if (m_remix && scene == m_remixScene && scene->Has2DFilters()) {
		/* the filters would work on an empty depth buffer */
		m_remix->NoteSkippedFilters();
	}
	else
#endif
	{
		GPU_pass_timer_begin(GPU_PASS_FILTERS);
		scene->Render2DFilters(m_canvas);
		GPU_pass_timer_end(GPU_PASS_FILTERS);
	}

#ifdef WITH_PYTHON
	PHY_SetActiveEnvironment(scene->GetPhysicsEnvironment());
	m_rasterizer->SyncOldMatrices();  /* BGE-Remix P8 Part B: for Python's bgl */
	scene->RunDrawingCallbacks(scene->GetPostDrawCB());

	// Python draw callback can also call debug draw functions, so we have to clear debug shapes.
	GPU_pass_timer_begin(GPU_PASS_OVERLAYS);
	m_rasterizer->FlushDebugShapes(scene);
	GPU_pass_timer_end(GPU_PASS_OVERLAYS);
#endif
}

void KX_KetsjiEngine::StopEngine()
{
	if (m_bInitialized)
	{
		m_sceneconverter->FinalizeAsyncLoads();

		if (m_animation_record)
		{
//			printf("TestHandlesPhysicsObjectToAnimationIpo\n");
			m_sceneconverter->TestHandlesPhysicsObjectToAnimationIpo();
		}

		ReleaseProbes();  /* BGE-Remix P8 Part B (P8.18) */

#ifdef WITH_REMIX
		/* the game's objects leave Remix (which keeps running for the next game) */
		delete m_remix;
		m_remix = NULL;
		m_remixScene = NULL;
		KX_RemixRenderer::Trace("game end: removing the scenes");
#endif

		KX_SceneList::iterator sceneit;
		for (sceneit = m_scenes.begin();sceneit != m_scenes.end() ; sceneit++)
		{
			KX_Scene* scene = *sceneit;
			m_sceneconverter->RemoveScene(scene);
		}

		/* BGE-Remix P8.5: Blender's objects give the texture coordinates again */
		GPU_texco_object_matrix_clear();
#ifdef WITH_REMIX
		KX_RemixRenderer::Trace("game end: scenes removed");
#endif
		m_scenes.clear();

		// cleanup all the stuff
		m_rasterizer->Exit();
	}
}

// Scene Management is able to switch between scenes
// and have several scenes running in parallel
void KX_KetsjiEngine::AddScene(KX_Scene* scene)
{ 
	m_scenes.push_back(scene);
	PostProcessScene(scene);
}



void KX_KetsjiEngine::PostProcessScene(KX_Scene* scene)
{
	bool override_camera = (m_overrideCam && (scene->GetName() == m_overrideSceneName));

	SG_SetActiveStage(SG_STAGE_SCENE);

	// if there is no activecamera, or the camera is being
	// overridden we need to construct a temporary camera
	if (!scene->GetActiveCamera() || override_camera)
	{
		KX_Camera* activecam = NULL;

		RAS_CameraData camdata = RAS_CameraData();
		if (override_camera)
		{
			camdata.m_lens = m_overrideCamLens;
			camdata.m_clipstart = m_overrideCamNear;
			camdata.m_clipend = m_overrideCamFar;
			
			camdata.m_perspective= !m_overrideCamUseOrtho;
		}
		activecam = new KX_Camera(scene,KX_Scene::m_callbacks,camdata);
		activecam->SetName("__default__cam__");
	
		// set transformation
		if (override_camera) {
			const MT_CmMatrix4x4& cammatdata = m_overrideCamViewMat;
			MT_Transform trans = MT_Transform(cammatdata.getPointer());
			MT_Transform camtrans;
			camtrans.invert(trans);
			
			activecam->NodeSetLocalPosition(camtrans.getOrigin());
			activecam->NodeSetLocalOrientation(camtrans.getBasis());
			activecam->NodeUpdateGS(0);
		} else {
			activecam->NodeSetLocalPosition(MT_Point3(0.0f, 0.0f, 0.0f));
			activecam->NodeSetLocalOrientation(MT_Vector3(0.0f, 0.0f, 0.0f));
			activecam->NodeUpdateGS(0);
		}

		scene->AddCamera(activecam);
		scene->SetActiveCamera(activecam);
		scene->GetObjectList()->Add(activecam->AddRef());
		scene->GetRootParentList()->Add(activecam->AddRef());
		// done with activecam
		activecam->Release();
	}
	
	scene->UpdateParents(0.0);
}



void KX_KetsjiEngine::RenderDebugProperties()
{
	STR_String debugtxt;
	int title_xmargin = -7;
	int title_y_top_margin = 4;
	int title_y_bottom_margin = 2;

	int const_xindent = 4;
	int const_ysize = 14;

	int xcoord = 12;	// mmmm, these constants were taken from blender source
	int ycoord = 17;	// to 'mimic' behavior
	
	int profile_indent = 72;

	float tottime = m_logger->GetAverage();
	if (tottime < 1e-6f) {
		tottime = 1e-6f;
	}

	// Set viewport to entire canvas
	RAS_Rect viewport;
	m_canvas->SetViewPort(0, 0, int(m_canvas->GetWidth()), int(m_canvas->GetHeight()));
	
	if (m_show_framerate || m_show_profile) {
		/* Title for profiling("Profile") */
		m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
		                            "Profile",
		                            xcoord + const_xindent + title_xmargin, // Adds the constant x indent (0 for now) to the title x margin
		                            ycoord,
		                            m_canvas->GetWidth() /* RdV, TODO ?? */,
		                            m_canvas->GetHeight() /* RdV, TODO ?? */);

		// Increase the indent by default increase
		ycoord += const_ysize;
		// Add the title indent afterwards
		ycoord += title_y_bottom_margin;
	}

	/* Framerate display */
	if (m_show_framerate) {
		m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
		                            "Frametime :",
		                            xcoord + const_xindent,
		                            ycoord,
		                            m_canvas->GetWidth() /* RdV, TODO ?? */,
		                            m_canvas->GetHeight() /* RdV, TODO ?? */);
		
		debugtxt.Format("%5.1fms (%.1ffps)", tottime * 1000.0f, 1.0f/tottime);
		m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
		                            debugtxt.ReadPtr(),
		                            xcoord + const_xindent + profile_indent,
		                            ycoord,
		                            m_canvas->GetWidth() /* RdV, TODO ?? */,
		                            m_canvas->GetHeight() /* RdV, TODO ?? */);
		// Increase the indent by default increase
		ycoord += const_ysize;
	}

	/* Profile display */
	if (m_show_profile) {
		for (int j = tc_first; j < tc_numCategories; j++) {
			m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
			                            m_profileLabels[j],
			                            xcoord + const_xindent,
			                            ycoord,
			                            m_canvas->GetWidth(),
			                            m_canvas->GetHeight());

			double time = m_logger->GetAverage((KX_TimeCategory)j);

			debugtxt.Format("%5.2fms | %d%%", (float)time*1000.f, (int)((float)time/tottime * 100.f));
			m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
			                            debugtxt.ReadPtr(),
			                            xcoord + const_xindent + profile_indent, ycoord,
			                            m_canvas->GetWidth(),
			                            m_canvas->GetHeight());

			m_rasterizer->RenderBox2D(xcoord + (int)(2.2f * profile_indent), ycoord, m_canvas->GetWidth(), m_canvas->GetHeight(), (float)time/tottime);
			ycoord += const_ysize;
		}
		/* BGE-Remix P8 Part B: the GPU time of each pass */
		for (int j = 0; j < GPU_PASS_COUNT; j++) {
			const float ms = GPU_pass_timer_ms((GPUPassTimer)j);
			debugtxt.Format("GPU %s:", GPU_pass_timer_name((GPUPassTimer)j));
			m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
			                            debugtxt.ReadPtr(),
			                            xcoord + const_xindent,
			                            ycoord,
			                            m_canvas->GetWidth(),
			                            m_canvas->GetHeight());
			if (ms >= 0.0f)
				debugtxt.Format("%5.2fms | %d%%", ms, (int)(ms / (tottime * 1000.f) * 100.f));
			else
				debugtxt = "  -";
			m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
			                            debugtxt.ReadPtr(),
			                            xcoord + const_xindent + profile_indent, ycoord,
			                            m_canvas->GetWidth(),
			                            m_canvas->GetHeight());
			if (ms >= 0.0f)
				m_rasterizer->RenderBox2D(xcoord + (int)(2.2f * profile_indent), ycoord, m_canvas->GetWidth(),
				                          m_canvas->GetHeight(), ms / (tottime * 1000.f));
			ycoord += const_ysize;
		}
	}
	// Add the ymargin for titles below the other section of debug info
	ycoord += title_y_top_margin;

	/* Property display */
	if (m_show_debug_properties) {

		/* Title for debugging("Debug properties") */
		m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
		                            "Debug Properties",
		                            xcoord + const_xindent + title_xmargin, // Adds the constant x indent (0 for now) to the title x margin
		                            ycoord,
		                            m_canvas->GetWidth() /* RdV, TODO ?? */,
		                            m_canvas->GetHeight() /* RdV, TODO ?? */);

		// Increase the indent by default increase
		ycoord += const_ysize;
		// Add the title indent afterwards
		ycoord += title_y_bottom_margin;

		/* Calculate amount of properties that can displayed. */
		unsigned propsAct = 0;
		unsigned propsMax = (m_canvas->GetHeight() - ycoord) / const_ysize;

		KX_SceneList::iterator sceneit;
		for (sceneit = m_scenes.begin();sceneit != m_scenes.end(); sceneit++) {
			KX_Scene* scene = *sceneit;
			/* the 'normal' debug props */
			vector<SCA_DebugProp*>& debugproplist = scene->GetDebugProperties();
			
			for (unsigned i=0; i < debugproplist.size() && propsAct < propsMax; i++)
			{
				CValue *propobj = debugproplist[i]->m_obj;
				STR_String objname = propobj->GetName();
				STR_String propname = debugproplist[i]->m_name;
				propsAct++;
				if (propname == "__state__") {
					// reserve name for object state
					KX_GameObject* gameobj = static_cast<KX_GameObject*>(propobj);
					unsigned int state = gameobj->GetState();
					debugtxt = objname + "." + propname + " = ";
					bool first = true;
					for (int statenum=1;state;state >>= 1, statenum++)
					{
						if (state & 1)
						{
							if (!first)
							{
								debugtxt += ",";
							}
							debugtxt += STR_String(statenum);
							first = false;
						}
					}
					m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
					                            debugtxt.ReadPtr(),
					                            xcoord + const_xindent,
					                            ycoord,
					                            m_canvas->GetWidth(),
					                            m_canvas->GetHeight());
					ycoord += const_ysize;
				}
				else {
					CValue *propval = propobj->GetProperty(propname);
					if (propval) {
						STR_String text = propval->GetText();
						debugtxt = objname + ": '" + propname + "' = " + text;
						m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED,
						                            debugtxt.ReadPtr(),
						                            xcoord + const_xindent,
						                            ycoord,
						                            m_canvas->GetWidth(),
						                            m_canvas->GetHeight());
						ycoord += const_ysize;
					}
				}
			}
		}
	}
}


KX_SceneList* KX_KetsjiEngine::CurrentScenes()
{
	return &m_scenes;
}



KX_Scene* KX_KetsjiEngine::FindScene(const STR_String& scenename)
{
	KX_SceneList::iterator sceneit = m_scenes.begin();

	// bit risky :) better to split the second clause 
	while ( (sceneit != m_scenes.end()) 
			&& ((*sceneit)->GetName() != scenename))
	{
		sceneit++;
	}

	return ((sceneit == m_scenes.end()) ? NULL : *sceneit);
}



void KX_KetsjiEngine::ConvertAndAddScene(const STR_String& scenename,bool overlay)
{
	// only add scene when it doesn't exist!
	if (FindScene(scenename)) {
		printf("warning: scene %s already exists, not added!\n",scenename.ReadPtr());
	}
	else {
		if (overlay) {
			m_addingOverlayScenes.push_back(scenename);
		}
		else {
			m_addingBackgroundScenes.push_back(scenename);
		}
	}
}




void KX_KetsjiEngine::RemoveScene(const STR_String& scenename)
{
	if (FindScene(scenename))
	{
		m_removingScenes.push_back(scenename);
	}
	else
	{
//		STR_String tmpname = scenename;
		std::cout << "warning: scene " << scenename << " does not exist, not removed!" << std::endl;
	}
}



void KX_KetsjiEngine::RemoveScheduledScenes()
{
	if (m_removingScenes.size())
	{
		vector<STR_String>::iterator scenenameit;
		for (scenenameit=m_removingScenes.begin();scenenameit != m_removingScenes.end();scenenameit++)
		{
			STR_String scenename = *scenenameit;

			KX_SceneList::iterator sceneit;
			for (sceneit = m_scenes.begin();sceneit != m_scenes.end() ; sceneit++)
			{
				KX_Scene* scene = *sceneit;
				if (scene->GetName()==scenename)
				{
					RemixRemoveScene(scene);
					m_sceneconverter->RemoveScene(scene);
					m_scenes.erase(sceneit);
					break;
				}
			}
		}
		m_removingScenes.clear();
	}
}

KX_Scene* KX_KetsjiEngine::CreateScene(Scene *scene, bool libloading)
{
	KX_Scene* tmpscene = new KX_Scene(m_keyboarddevice,
									  m_mousedevice,
									  m_networkdevice,
									  scene->id.name+2,
									  scene,
									  m_canvas);

	m_sceneconverter->ConvertScene(tmpscene,
							  m_rasterizer,
							  m_canvas,
							  libloading);

	return tmpscene;
}

KX_Scene* KX_KetsjiEngine::CreateScene(const STR_String& scenename)
{
	Scene *scene = m_sceneconverter->GetBlenderSceneForName(scenename);
	if (!scene)
		return NULL;
	return CreateScene(scene);
}

void KX_KetsjiEngine::AddScheduledScenes()
{
	vector<STR_String>::iterator scenenameit;

	if (m_addingOverlayScenes.size())
	{
		for (scenenameit = m_addingOverlayScenes.begin();
			scenenameit != m_addingOverlayScenes.end();
			scenenameit++)
		{
			STR_String scenename = *scenenameit;
			KX_Scene* tmpscene = CreateScene(scenename);
			if (tmpscene) {
				m_scenes.push_back(tmpscene);
				PostProcessScene(tmpscene);
			} else {
				printf("warning: scene %s could not be found, not added!\n",scenename.ReadPtr());
			}
		}
		m_addingOverlayScenes.clear();
	}
	
	if (m_addingBackgroundScenes.size())
	{
		for (scenenameit = m_addingBackgroundScenes.begin();
			scenenameit != m_addingBackgroundScenes.end();
			scenenameit++)
		{
			STR_String scenename = *scenenameit;
			KX_Scene* tmpscene = CreateScene(scenename);
			if (tmpscene) {
				m_scenes.insert(m_scenes.begin(),tmpscene);
				PostProcessScene(tmpscene);
			} else {
				printf("warning: scene %s could not be found, not added!\n",scenename.ReadPtr());
			}
		}
		m_addingBackgroundScenes.clear();
	}
}



bool KX_KetsjiEngine::ReplaceScene(const STR_String& oldscene,const STR_String& newscene)
{
	// Don't allow replacement if the new scene doesn't exist.
	// Allows smarter game design (used to have no check here).
	// Note that it creates a small backward compatbility issue
	// for a game that did a replace followed by a lib load with the
	// new scene in the lib => it won't work anymore, the lib
	// must be loaded before doing the replace.
	if (m_sceneconverter->GetBlenderSceneForName(newscene) != NULL) {
		m_replace_scenes.push_back(std::make_pair(oldscene,newscene));
		return true;
	}
	return false;
}

// replace scene is not the same as removing and adding because the
// scene must be in exact the same place (to maintain drawingorder)
// (nzc) - should that not be done with a scene-display list? It seems
// stupid to rely on the mem allocation order...
void KX_KetsjiEngine::ReplaceScheduledScenes()
{
	if (m_replace_scenes.size())
	{
		vector<pair<STR_String,STR_String> >::iterator scenenameit;
		
		for (scenenameit = m_replace_scenes.begin();
			scenenameit != m_replace_scenes.end();
			scenenameit++)
		{
			STR_String oldscenename = (*scenenameit).first;
			STR_String newscenename = (*scenenameit).second;
			int i=0;
			/* Scenes are not supposed to be included twice... I think */
			KX_SceneList::iterator sceneit;
			for (sceneit = m_scenes.begin();sceneit != m_scenes.end() ; sceneit++) {
				KX_Scene* scene = *sceneit;
				if (scene->GetName() == oldscenename) {
					// avoid crash if the new scene doesn't exist, just do nothing
					Scene *blScene = m_sceneconverter->GetBlenderSceneForName(newscenename);
					if (blScene) {
						RemixRemoveScene(scene);
						m_sceneconverter->RemoveScene(scene);
						KX_Scene* tmpscene = CreateScene(blScene);
						m_scenes[i]=tmpscene;
						PostProcessScene(tmpscene);
					}
					else {
						printf("warning: scene %s could not be found, not replaced!\n",newscenename.ReadPtr());
					}
				}
				i++;
			}
		}
		m_replace_scenes.clear();
	}
}



void KX_KetsjiEngine::SuspendScene(const STR_String& scenename)
{
	KX_Scene*  scene = FindScene(scenename);
	if (scene) scene->Suspend();
}



void KX_KetsjiEngine::ResumeScene(const STR_String& scenename)
{
	KX_Scene*  scene = FindScene(scenename);
	if (scene) scene->Resume();
}



void KX_KetsjiEngine::SetUseFixedTime(bool bUseFixedTime)
{
	m_bFixedTime = bUseFixedTime;
}

void KX_KetsjiEngine::SetUseExternalClock(bool useExternalClock)
{
	m_useExternalClock = useExternalClock;
}

void	KX_KetsjiEngine::SetAnimRecordMode(bool animation_record, int startFrame)
{
	m_animation_record = animation_record;
	if (animation_record)
	{
		// when recording physics keyframes, run at a variable (capped) frame rate (fixed time == full speed)
		m_bFixedTime = false;
	}
	m_currentFrame = startFrame;
}

int KX_KetsjiEngine::getAnimRecordFrame() const
{
	return m_currentFrame;
}

void KX_KetsjiEngine::setAnimRecordFrame(int framenr)
{
	m_currentFrame = framenr;
}

bool KX_KetsjiEngine::GetUseFixedTime(void) const
{
	return m_bFixedTime;
}

bool KX_KetsjiEngine::GetUseExternalClock(void) const
{
	return m_useExternalClock;
}

double KX_KetsjiEngine::GetSuspendedDelta()
{
	return m_suspendeddelta;
}

double KX_KetsjiEngine::GetTicRate()
{
	return m_ticrate;
}

void KX_KetsjiEngine::SetTicRate(double ticrate)
{
	m_ticrate = ticrate;
}

double KX_KetsjiEngine::GetTimeScale() const
{
	return m_timescale;
}

void KX_KetsjiEngine::SetTimeScale(double timescale)
{
	m_timescale = timescale;
}

int KX_KetsjiEngine::GetMaxLogicFrame()
{
	return m_maxLogicFrame;
}

void KX_KetsjiEngine::SetMaxLogicFrame(int frame)
{
	m_maxLogicFrame = frame;
}

int KX_KetsjiEngine::GetMaxPhysicsFrame()
{
	return m_maxPhysicsFrame;
}

void KX_KetsjiEngine::SetMaxPhysicsFrame(int frame)
{
	m_maxPhysicsFrame = frame;
}

bool KX_KetsjiEngine::GetRestrictAnimationFPS()
{
	return m_restrict_anim_fps;
}

void KX_KetsjiEngine::SetRestrictAnimationFPS(bool bRestrictAnimFPS)
{
	m_restrict_anim_fps = bRestrictAnimFPS;
}

double KX_KetsjiEngine::GetAnimFrameRate()
{
	return m_anim_framerate;
}

double KX_KetsjiEngine::GetClockTime(void) const
{
	return m_clockTime;
}

void KX_KetsjiEngine::SetClockTime(double externalClockTime)
{
	m_clockTime = externalClockTime;
}

double KX_KetsjiEngine::GetFrameTime(void) const
{
	return m_frameTime;
}

double KX_KetsjiEngine::GetRealTime(void) const
{
	return m_kxsystem->GetTimeInSeconds();
}

void KX_KetsjiEngine::SetAnimFrameRate(double framerate)
{
	m_anim_framerate = framerate;
}

double KX_KetsjiEngine::GetAverageFrameRate()
{
	return m_average_framerate;
}

void KX_KetsjiEngine::SetExitKey(short key)
{
	m_exitkey = key;
}

short KX_KetsjiEngine::GetExitKey()
{
	return m_exitkey;
}

void KX_KetsjiEngine::SetRender(bool render)
{
	m_doRender = render;
}

bool KX_KetsjiEngine::GetRender()
{
	return m_doRender;
}

void KX_KetsjiEngine::SetShowFramerate(bool frameRate)
{
	m_show_framerate = frameRate;
}

bool KX_KetsjiEngine::GetShowFramerate()
{
	return m_show_framerate;
}

void KX_KetsjiEngine::SetShowProfile(bool profile)
{
	m_show_profile = profile;
}

bool KX_KetsjiEngine::GetShowProfile()
{
	return m_show_profile;
}

void KX_KetsjiEngine::SetShowProperties(bool properties)
{
	m_show_debug_properties = properties;
}

bool KX_KetsjiEngine::GetShowProperties()
{
	return m_show_debug_properties;
}

void KX_KetsjiEngine::SetAutoAddDebugProperties(bool add)
{
	m_autoAddDebugProperties = add;
}

bool KX_KetsjiEngine::GetAutoAddDebugProperties()
{
	return m_autoAddDebugProperties;
}

void KX_KetsjiEngine::SetTimingDisplay(bool frameRate, bool profile, bool properties)
{
	m_show_framerate = frameRate;
	m_show_profile = profile;
	m_show_debug_properties = properties;
}



void KX_KetsjiEngine::GetTimingDisplay(bool& frameRate, bool& profile, bool& properties) const
{
	frameRate = m_show_framerate;
	profile = m_show_profile;
	properties = m_show_debug_properties;
}



void KX_KetsjiEngine::ProcessScheduledScenes(void)
{
	// Check whether there will be changes to the list of scenes
	if (m_addingOverlayScenes.size() ||
		m_addingBackgroundScenes.size() ||
		m_replace_scenes.size() ||
		m_removingScenes.size()) {

		// Change the scene list
		ReplaceScheduledScenes();
		RemoveScheduledScenes();
		AddScheduledScenes();
	}
}


void KX_KetsjiEngine::SetHideCursor(bool hideCursor)
{
	m_hideCursor = hideCursor;
}


bool KX_KetsjiEngine::GetHideCursor(void) const
{
	return m_hideCursor;
}


void KX_KetsjiEngine::SetUseOverrideFrameColor(bool overrideFrameColor)
{
	m_overrideFrameColor = overrideFrameColor;
}


bool KX_KetsjiEngine::GetUseOverrideFrameColor(void) const
{
	return m_overrideFrameColor;
}


void KX_KetsjiEngine::SetOverrideFrameColor(float r, float g, float b, float a)
{
	m_overrideFrameColorR = r;
	m_overrideFrameColorG = g;
	m_overrideFrameColorB = b;
	m_overrideFrameColorA = a;
}


void KX_KetsjiEngine::GetOverrideFrameColor(float& r, float& g, float& b, float& a) const
{
	r = m_overrideFrameColorR;
	g = m_overrideFrameColorG;
	b = m_overrideFrameColorB;
	a = m_overrideFrameColorA;
}


void KX_KetsjiEngine::Resize()
{
	KX_SceneList::iterator sceneit;

	/* extended mode needs to recalculate camera frusta when */
	KX_Scene* firstscene = *m_scenes.begin();
	const RAS_FrameSettings &framesettings = firstscene->GetFramingType();

	if (framesettings.FrameType() == RAS_FrameSettings::e_frame_extend) {
		for (sceneit = m_scenes.begin(); sceneit != m_scenes.end(); sceneit++) {
			KX_Camera* cam = ((KX_Scene *)*sceneit)->GetActiveCamera();
			cam->InvalidateProjectionMatrix();
		}
	}
}


void KX_KetsjiEngine::SetGlobalSettings(GlobalSettings* gs)
{
	m_globalsettings.matmode = gs->matmode;
	m_globalsettings.glslflag = gs->glslflag;
}

GlobalSettings* KX_KetsjiEngine::GetGlobalSettings(void)
{
	return &m_globalsettings;
}


/* BGE-Remix */

void KX_KetsjiEngine::SetRemixOptions(int enabled, int fast, bool window_mode_allowed)
{
	m_remixOverride = enabled;
	m_remixFastOverride = fast;
	m_remixWindowModeAllowed = window_mode_allowed;
}

void KX_KetsjiEngine::RenderRemixNotice(const char *text)
{
	m_canvas->SetViewPort(0, 0, int(m_canvas->GetWidth()), int(m_canvas->GetHeight()));
	m_rasterizer->RenderText2D(RAS_IRasterizer::RAS_TEXT_PADDED, text, 12, int(m_canvas->GetHeight()) - 12,
	                           m_canvas->GetWidth(), m_canvas->GetHeight());
}

void KX_KetsjiEngine::RemixRemoveScene(KX_Scene *scene)
{
#ifdef WITH_REMIX
	if (m_remix)
		m_remix->RemoveScene(scene);
	if (scene == m_remixScene)
		m_remixScene = NULL;
#else
	(void)scene;
#endif
}
