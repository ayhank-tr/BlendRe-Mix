# The Remix View's Edit and Run modes (P10.6 to P10.9)

Notes for Ayhan, to be merged into the guide at sign-off.

## Four modes

The Remix View's header has four mode buttons: Check, Debug, Edit and Run.
Each mode shows only its own tools in the sidebar (N):

| Mode  | Sidebar panels                                  |
|-------|-------------------------------------------------|
| Check | Element, Warnings, Targets, Layers              |
| Debug | Targets, Inspect, RTX Remix Statistics          |
| Edit  | Edit Element, Settings Hub                      |
| Run   | Run                                             |

The holograms show in Check, Edit and Run (not in Debug, as before).

## Edit mode

- **Click an avatar** (left click, or your select button) to select its
  element. Its object becomes the active object, so Blender's own editors
  (Properties, Logic) show the same thing.
- The **Edit Element** panel shows its settings, drawn from Blender's own
  data: a logic brick's settings, a body's physics (Blender's own Physics
  panels, drawn here), a sound's actuator and sound, a lamp's and a
  material's panels, a game property's value. A change here shows in
  Blender's editors and the other way round; undo works.
- **Drag from one avatar to another** to wire them:
  - a sensor to a controller, a controller to an actuator (either way round);
  - an actuator to the object it acts on (Track To, Add Object, Camera,
    Parent, Property Copy, Steering, Armature, a servo Motion's reference,
    a Scene actuator's camera, a Message actuator's "To");
  - a message to its listener (the listener's Message sensor gets the
    message's subject);
  - a child to its parent (the child stays where it is).
  A pair that cannot be wired is refused with a note in the status bar.
- **The Element menu** (header, the panel's menu button, or W over the view)
  adds a sensor, controller, actuator, body or sound to the selected
  avatar's object (or the active object), and removes the selected brick,
  body or sound.
- The warnings follow every edit (the model is rebuilt).

## The settings hub

The **Settings Hub** panel (Edit mode) lists every setting that affects the
game, once, grouped by your clusters (Entity, Factor, Behaviour, Gate) and
by layer: the scene's game settings, the pipeline and RTX Remix's
settings, the physics engine, the world, and each object's game settings
(one group per object, inside each layer). Each row is the setting itself;
the small icon beside it opens the Properties editor at the tab where
Blender also shows it. A row without an icon is shown only here (Blender's
panels show it only for other choices, such as the dome settings while
Stereo is off). Click a group's name to open or close it; the search field
finds settings by name.

How the hub knows where Blender shows a setting: it runs Blender's own
Properties panels with a stand-in layout that records what they draw. So the
links follow Blender's panels by themselves, and add-ons' game panels are
found too.

## Run mode

- **Run** plays the game inside the Remix View's own area. The header's
  choice: **Whole Scene**, or **Chosen Objects**: a mock-up of the selected
  objects plus what they need (their logic's targets, their parents and
  children, the objects they are jointed to, the camera, the lamps; the Run
  panel lists them and why). The rest is not in the game.
- While it plays, the **holograms stay over the game** and show its live
  state: each brick's colour (P10.10's states), what fired this frame (a
  green ring), each property's value and each body's contacts beside its
  name. The holograms are drawn over the picture (not hidden behind walls).
- **Suspend**: the Suspend button over the game (top left), or the Pause
  key. **Stop**: the Stop button, or the game's exit key (Esc). Nothing can
  be edited while the game plays. A game that hides the mouse cursor hides
  it here too: use the Pause key then.
- When suspended, the view goes to **Edit mode**, with the game frozen: the
  objects are where the game had them, the properties hold the game's
  values. Edit anything. The header shows "Suspended" with **Continue...**
  and **Stop...** in every mode.
- **Continue...** asks: **Resume with the changes** (the game goes on from
  where it was, with what you changed) or **Start over** (the game starts
  again from the beginning, with what you changed; what you did not change
  goes back to how the file had it).
- Resume takes: property values, places (location, rotation and scale), the
  logic's settings and links, the physics settings, and (with no extra
  work, see below) any other setting. It cannot take a new or removed
  object, a new or removed brick (or one whose type changed), a new or
  removed game property, a new parent, or a game that had gone to another
  scene: then only Start over is offered, and the pop-up says why.
- **Stop...** asks whether to keep the changes made while suspended.
  **Yes** keeps them (what the game itself moved goes back to the file's
  values); **No** puts the file back exactly as it was when Run started.
  If the game was never suspended, nothing changed and nothing is asked.

### How suspend works

Blender 2.79's game inside Blender blocks Blender until it ends. Suspend
ends the game (as Esc does) after keeping a **snapshot** of its state, by
object names: every object's place, velocity, logic states, visibility and
properties, the objects the game added and ended, the active camera. The
snapshot's places and property values are written into the file, so you
edit the frozen game. Resume starts the game again from the file (with
your changes) and puts the rest of the snapshot back (velocities, states,
added and ended objects, the camera).

Because the game is really gone while suspended, nothing can crash if the
file changes then: undo, deleting objects, even opening another file (the
run is then forgotten). What a snapshot cannot hold starts afresh on
Resume: Python's own variables (anything a script keeps outside game
properties), actions part-way through, sounds, delay and random sensors'
counters, constraints made by scripts.

When Run starts, the file is kept in memory (as an undo step is); Stop,
then No, puts it back. That is itself an undo step, so it can be undone.

Saving while suspended saves the game's frozen places and values.

## Known differences in RTX Remix

- The holograms over the game are drawn after RTX Remix's picture, into the
  same frame; Remix's picture has no depth, so the holograms are always in
  front (in Check and Edit mode they hide behind walls).
- RTX Remix keeps running between Suspend and Resume (the game ends, Remix
  does not), so Resume does not wait for Remix to start again; the scene is
  sent to Remix again, so its textures take their usual moment.
