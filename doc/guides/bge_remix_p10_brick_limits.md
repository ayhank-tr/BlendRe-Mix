# P10.13: the logic bricks' limits

You approved this list on 6 Nov, and the approved entries are now built.
Each entry starts with its outcome:
- **Fixed**: the fix keeps old games playing exactly as before.
- **Option**: stored in the file, off for old files, so old games play as
  before; it changes play only when turned on.
- **Warning**: the Remix View's logic layer flags it; the game is unchanged.
- **Not done**: not approved, or tested first.

Every option is kept in the file in a way stock 2.79b keeps and ignores, so
the files still open there (test T10.c20).

Line numbers are in the BGE-Remix tree (branch part/p10c). The engine code
named here is stock 2.79b code: BGE-Remix never changed it before P10.

Each entry gives:
- **What**: the limit, in plain words.
- **Evidence**: a link or a file:line.
- **Who**: whom it affects.
- **Fix**: the fix proposed.
- **Old games**: whether the fix keeps old games playing as before.

## A. The bricks themselves (stock 2.79b)

### A1. One pulse a tic, however many messages or collisions arrive
- **Option** (built, test T10.c15): the Property actuator's Add mode has **Add Once per Message**, off by default. It counts the messages its Message sensors caught that tic. Collisions are not counted: the approved fix was "per message".
- **What**: when two messages (or two collisions) arrive in the same logic tic, the sensor fires once. A Property actuator that adds 1 then counts 1, not 2.
- **Evidence**:
  - [projects.blender.org issue 39493](https://projects.blender.org/blender/blender/issues/39493), closed as "by design".
  - `Ketsji/KXNetwork/KX_NetworkMessageSensor.cpp:105-149`: every message is collected, and one trigger is sent.
- **Who**: games that count hits, pickups or messages with bricks only.
- **Fix**: a new option, "Add once per message", on the Property actuator's Add mode, fed by the sensor's message or hit count. It is off by default.
- **Old games**: yes, since the option is off unless you turn it on.

### A2. A held key gives no event when its state is entered
- **Option** (built, test T10.c16): the Keyboard sensor's **Read Held Keys**, off by default. Found while building: with Level on, stock already fires for a held key when the state is entered. The gap is the plain (edge) sensor, which is what the option covers.
- **What**: a Keyboard sensor that becomes active (its state is entered) while its key is already held does not fire until the key is released and pressed again.
- **Evidence**: the code says so itself, at `GameLogic/SCA_KeyboardSensor.cpp:80-88` ("if the target key is pressed when the sensor is reactivated, it will not generate an event").
- **Who**: state machines that use the same key in several states, such as running across a walk and jump state.
- **Fix**: with the sensor's Level option on, read the key's current state when the sensor becomes active.
- **Old games**: only if Level-on keyboard sensors are allowed to change. The safer choice is a new option, "Read held keys", off by default.

### A3. A controller can be in only one state from the editor
- **Fixed** (built, test T10.c11): in the logic editor's state menu, Shift+click adds or removes a state. A plain click still picks one state, as before. The header shows "2+" for a controller in several states. Python: `controller.state_mask`, 30 booleans; `controller.states` stays its first state.
- **What**: the engine and the file format let a controller belong to several states (a mask). The logic editor and Python's `controller.states` only set one state number, so the same bricks get copied into each state.
- **Evidence**:
  - `blender/makesrna/intern/rna_controller.c:251-263`: the boolean array was commented out, and `states` is a number from 1 to 30.
  - `GameLogic/SCA_IController.cpp` ApplyState uses the full mask.
- **Who**: anyone building state machines with bricks.
- **Fix**: a state mask in the editor and in the Remix View's Edit mode (the Remix View's groups already show a controller in several states).
- **Old games**: yes. The data is unchanged, and a mask with one bit is today's single state.

### A4. The order in which objects' bricks run is not defined
- **Fixed** (built, test T10.c12): **Logic Priority** per object, in the logic editor's sidebar (Logic Order panel) and in the Remix View's Starting States panel. A higher number runs first. At 0 (every old file) the engine keeps stock's order and does not sort at all.
- **What**: the "Mark" (priority) option puts a controller ahead of its own object's other controllers. It cannot order objects. Between objects, the order is the order in which their sensors fired that tic.
- **Evidence**:
  - [blenderartists: "Ensure one script runs before everything else"](https://blenderartists.org/t/ensure-one-script-runs-before-everything-else/646159).
  - `GameLogic/SCA_IController.h:87-101`: marked controllers are put first, and objects are listed as they were triggered.
- **Who**: games whose scripts depend on each other: set-up scripts, managers.
- **Fix**: a priority number per object, in the scene's game settings. The default 0 keeps today's order.
- **Old games**: yes, with every object at 0.

### A5. Leaving a state stops its actuators one tic late
- **Option** (built, test T10.c17): the State actuator's **Stop Left State Now**, off by default. An actuator pulsed in that same tic keeps its pulse.
- **What**: when a State actuator leaves a state, an actuator of that state that is still running (a motion, a servo, a sound) gets its stop pulse on the next tic. It runs one more tic.
- **Evidence**:
  - `GameLogic/SCA_LogicManager.cpp:238-250` (the IsNoLink branch: "make sure it will get a negative event on next frame to stop it").
  - Our test T10.c1 sees it: A2_servo is still Blue on tic 92, and Black on tic 93.
- **Who**: precise state machines, for example a motion that must stop exactly when the state changes.
- **Fix**: an option on the State actuator, "Stop the left state's actuators now", off by default.
- **Old games**: yes, since the option is off by default. Changing the default would move timings by one tic.

### A6. Timers run slow when the game cannot keep up
- **Option** (built, test T10.c18): the scene's **Timers Follow Game Time** (World tab, Physics panel), off by default. Found while building: in stock it can never matter. Stock caps the tics at Max Logic Steps before they could stand for more time (A7), so this option only does something with Own Max Physics Steps on.
- **What**: when frames are slow, the engine fits several tics' worth of game time into one logic tic (Max Logic Steps). Timer properties, the new Timer sensor and an added object's life time still count only one tic's time (1 / tic rate). They fall behind the game's clock.
- **Evidence**:
  - `Ketsji/KX_Scene.cpp:1651`: `BeginFrame(curtime, 1.0/KX_KetsjiEngine::GetTicRate())`.
  - `Ketsji/KX_KetsjiEngine.cpp:792`: framestep grows when frames exceed the maximum.
  - `Ketsji/KX_Scene.cpp:1634`: an added object's life ("timebomb").
- **Who**: games that run slower than their tic rate and use timers or object life times.
- **Fix**: pass the tic's real game time (framestep).
- **Old games**: no, a slow old game's timers would change. Offer it as an option per scene, "Timers follow game time", off by default.

### A7. "Max Physics Steps" is ignored: Max Logic Steps is used instead
- **Option** (built, test T10.c18): the scene's **Own Max Physics Steps**. It is off for old files (no setting in them), so they keep stock's behaviour. It is on for new scenes and new files. It only matters when the two numbers differ (both are 5 by default) and the game runs slow.
- **What**: the engine reads the scene's maximum logic steps where it means the maximum physics steps. When the game is slow, the physics catch-up limit (and the game time skipped) follows the wrong setting.
- **Evidence**:
  - `Ketsji/KX_KetsjiEngine.cpp:507`: `m_maxPhysicsFrame = scene->gm.maxphystep ? scene->gm.maxlogicstep : 5;`.
  - The same line is in stock 2.79b (commit a14022a, line 444).
- **Who**: games that set the two values differently, and only when they run slow.
- **Fix**: read `maxphystep` (a one-word fix).
- **Old games**: no, for games with different values, and only when slow. This could also be offered as an option. Your choice.

### A8. The Delay sensor stops at 5000 tics in the editor
- **Fixed** (built, test T10.c13): the editor takes up to 32767 tics, not the 100000 proposed. The file keeps the delay as a 16-bit number, so 32767 is as far as it goes without a new field (about 9 minutes at 60 tics a second). The Timer sensor (P10.12) covers longer waits.
- **What**: the editor allows a delay and duration of 5000 tics at most (83 s at 60 tics a second). Python allows 100000.
- **Evidence**:
  - `blender/makesrna/intern/rna_sensor.c:617,624`.
  - `GameLogic/SCA_DelaySensor.cpp:162`.
- **Who**: long waits made with bricks (the new Timer sensor, in seconds, covers most of them).
- **Fix**: raise the editor's range to 100000.
- **Old games**: yes.

### A9. The Expression controller holds 127 characters
- **Warning** (built, test T10.c19): LOG_EXPR_FULL, when the expression fills all 127 characters.
- **What**: a longer expression is cut off.
- **Evidence**:
  - `blender/makesdna/DNA_controller_types.h:44` (`char str[128]`).
  - [blenderartists: "BGE/UPBGE expression controller stops to work"](https://blenderartists.org/t/bge-upgbe-expression-controller-stops-to-work/1167039).
- **Who**: long conditions written as expressions.
- **Fix**: a Remix View warning near the limit. A longer field would not open the same way in stock 2.79b.
- **Old games**: yes.

### A10. In an expression, a property beats a sensor of the same name
- **Warning** (built, test T10.c19): LOG_EXPR_NAME_CLASH. Found while building: the code works the other way round from the forum thread. A sensor *linked to the controller* wins over a property of the same name (`SCA_ExpressionController::FindIdentifier`). A sensor *not linked* to it cannot be read at all, and the property of that name is read instead. The warning covers both cases.
- **What**: when a sensor and a property share a name, the expression reads the property.
- **Evidence**: [the same blenderartists thread](https://blenderartists.org/t/bge-upgbe-expression-controller-stops-to-work/1167039).
- **Who**: expressions naming sensors.
- **Fix**: a Remix View warning on the clash.
- **Old games**: yes.

### A11. Near and Radar sensors only see Actor objects
- **Warning** (built, test T10.c19): LOG_SEES_ACTORS, when no other object in the scene has Actor on.
- **What**: a Near or Radar sensor ignores objects whose physics has Actor off, with no hint why.
- **Evidence**: `Ketsji/KX_TouchSensor.cpp:216-219` ("only with actor objects").
- **Who**: anyone using Near or Radar sensors.
- **Fix**: a Remix View warning when no object it could see is an Actor.
- **Old games**: yes.

### A12. Collision sensors may miss hits with physics substeps above 1
- **Not done**: test first (not approved).
- **What**: this was reported, but never confirmed by a developer.
- **Evidence**: [developer.blender.org T39075](https://developer.blender.org/T39075), item 1.
- **Who**: fast objects with substeps.
- **Fix**: first a test scene (a fast ball, substeps 1 and 4, hits counted), then a fix if it is real.
- **Old games**: unknown until tested.

### A13. The Message sensor cannot filter by sender or body
- **Fixed** (built, test T10.c14): the Message sensor has **From** (an object) and **Body** filters. Empty means any, as in every old file. The Remix View's message arrows follow them. Python: `fromObject` and `body` on the sensor. Stock 2.79b keeps both and ignores them.
- **What**: the file keeps a sender and a body for the Message sensor. The engine never uses them.
- **Evidence**: `blender/makesdna/DNA_sensor_types.h:146-161` ("possible future use").
- **Who**: games where several objects send the same subject.
- **Fix**: use the sender and body as filters (empty means any, as today).
- **Old games**: yes, since both are empty in old files.

### A14. An Actuator sensor whose actuator does not exist never fires, silently
- **Warning** (built, test T10.c19): LOG_NO_ACTUATOR, shown Red. The pick list was already there in stock: the Actuator sensor's field lists its object's actuators.
- **What**: it watches an actuator of its own object, by name. A wrong name, or an actuator on another object, gives a sensor that never fires.
- **Evidence**:
  - `GameLogic/SCA_ActuatorSensor.cpp` (Init finds the actuator by name on its own object).
  - P10.10 now shows it Red while the game runs.
- **Who**: logic watching actuators.
- **Fix**: a Remix View warning (Red) when the file is checked, and a pick list of the object's actuators in the editor (as P10.11's subjects).
- **Old games**: yes.

### A15. A Python script that fails to compile stays off, silently after the first error
- **Not done**: not approved (the Remix View already warns).
- **What**: the error is printed once. After that, the controller does nothing for the rest of the game.
- **Evidence**: `GameLogic/SCA_PythonController.cpp:414-418`.
- **Who**: scripts edited while testing.
- **Fix**: none needed in the engine. The Remix View already warns (PY_SYNTAX), and P10.10 shows the controller Red.
- **Old games**: not applicable.

### A16. A Shape Action with no matching shape keys plays forever, doing nothing
- **Warning** (built, test T10.c19): LOG_SHAPE_NO_KEYS. Found while building: 2.79 makes no Shape Action actuators; the Action actuator plays shape-key actions. So the warning covers both: an action that animates shape keys, none of them the mesh's.
- **What**: the actuator cannot tell that its action moves no shape key.
- **Evidence**: `Converter/BL_ShapeActionActuator.cpp:447` ("in 2.5 theres no way to do this").
- **Who**: shape key animation with bricks.
- **Fix**: a Remix View warning when the action's channels match none of the mesh's keys.
- **Old games**: yes.

### A17. An object added from a scaled child comes out scaled
- **Not done**: test first (not approved).
- **What**: this was reported, and it is unclear whether it is a bug.
- **Evidence**: [developer.blender.org T39075](https://developer.blender.org/T39075), item 7.
- **Who**: Add Object from parented spawners.
- **Fix**: a test scene first.
- **Old games**: any change would change old games, so this would be an option only.

### A18. At most eight joysticks
- **Not done**: not approved.
- **What**: joystick index 0 to 7.
- **Evidence**: `GameLogic/Joystick/SCA_JoystickDefines.h:46` (JOYINDEX_MAX 8).
- **Who**: few people.
- **Fix**: none proposed.
- **Old games**: not applicable.

### A19. Old 2.4x files may address messages as "OBname"
- **Not done**: not approved.
- **What**: the Remix View strips an "OB" prefix from a Message actuator's To field. The engine compares the field with the object's name without the prefix. If 2.4x files stored the prefix, their addressed messages never arrive.
- **Evidence**:
  - `BlenderRoutines/BL_GameViewLayers.cpp:2095`.
  - `Ketsji/KXNetwork/KX_NetworkMessageSensor.cpp:105-109`.
- **Who**: games converted from 2.4x.
- **Fix**: check with a real 2.4x file. If confirmed, strip the prefix when the file is read (versioning).
- **Old games**: yes for 2.79 files. 2.4x files would start working.

## B. What the Remix View's logic layer does with them

### B1. Bricks are known by "object/name" across sensors, controllers and actuators
- **Fixed** (built, test T10.c19): bricks are now keyed by object, kind and name. Run mode (P10.9) already finds bricks by kind and name, so it needed no change.
- **What**: a sensor and an actuator with the same name share one key. Blender's default names clash: "Property", "Message", "Mouse" and "Armature" are both sensor and actuator names. A warning can land on the wrong brick, and "the converter made nothing for it" can be missed.
- **Evidence**:
  - `BlenderRoutines/BL_GameViewLayers.cpp:1597,1632,1669`.
  - `BlenderRoutines/BL_GameView.cpp:447-453`.
- **Who**: every file with default brick names. It also matters to Run mode (p10b), which must find each brick's live state by name.
- **Fix**: key bricks by kind as well (P10.11's new code already does).
- **Old games**: yes, since only the view changes.

### B2. A missing target is Black in the Remix View, but Red in P10.10's table
- **Fixed** (built, test T10.c19): LOG_NO_TARGET is Red, and so is a Property sensor's missing property (DAT_PROP_MISSING). Two new Red checks: LOG_NO_ACTUATOR (an Actuator sensor's missing actuator) and LOG_NO_FILE (a Start Game file that is not there). P7's own test now expects Red for LOG_NO_TARGET.
- **What**: P7's check marks "no object chosen" Black ("not set up"). P10.10's table, and the live state, say Red. The build does not flag:
  - a Game actuator's missing file;
  - an Actuator sensor's missing actuator;
  - a Property sensor's missing property (the Data layer notes a missing property, Black).
- **Evidence**: `BlenderRoutines/BL_GameViewLayers.cpp` Logic() (LOG_NO_TARGET, GV_WARN_BLACK).
- **Who**: anyone reading the colours.
- **Fix**: make LOG_NO_TARGET Red, and add the three checks.
- **Old games**: yes, since only the view changes.

### B3. What the logic layer finds in our test scenes
This is expected from the code. The probe's results (T10.c4's detail) list the real warnings.
- `p10c_missing`:
  - LOG_NO_TARGET (Black) on M_track, M_add and M_parent;
  - PY_NO_SCRIPT (Black) on M_script;
  - nothing for M_game (missing file) or M_actsens (missing actuator): see B2.
- `p10c_states`:
  - MSG_NO_LISTENER on "lost";
  - RMX_ACTUATOR_CLASSIC (new, P10.12) on X_remix;
  - no LOG_STATE_NEVER (every state is entered).
