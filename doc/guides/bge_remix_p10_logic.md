# P10, third track: the logic bricks brought up to date (P10.10-P10.13)

The bricks stay, and old games play as before. Node logic is not part of
P10. The bricks' old limits (P10.13) are listed in
`bge_remix_p10_brick_limits.md`; the approved ones are built (the last
section below).

## P10.10: each brick's live state

While a game runs, each brick has a state in your colours. Run mode (the
Remix View's P10.9) draws it on the brick's avatar. The engine only reads
the bricks to find their state: a game plays exactly the same with or
without it (test T10.c3).

### Every brick

| Colour | When |
|---|---|
| Red | Its object, target or file is missing. This covers: no object chosen (Track To, Add Object, Camera, Set Parent, Copy Property); no action (Action, Shape Action); no target or navigation mesh (Steering); no sound, or a sound file that is not there; a Start Game file that is not there; no scene (Set or Add Scene) or camera; a custom 2D filter with no text. Also a target that ended during the game. |

### Sensors

| Colour | When |
|---|---|
| Red | Any reason under "Every brick". Also: an Actuator sensor watching an actuator its object does not have, or a Property sensor watching a property its object does not have. |
| Green | It fired a positive pulse this tic, or it is positive (a key held, an Always sensor). |
| Black | No controller of a state the object is in listens to it: the engine does not run it. Also when it is suspended. |
| Yellow | A Delay sensor counting, or a Timer sensor not yet due. |
| White | Ready, not positive. |

### Controllers

| Colour | When |
|---|---|
| Red | Any reason under "Every brick". Also a Python controller with no script, or one that failed to compile or import. |
| Green | It ran this tic and sent "on" (And, Or, Nand, Nor, Xor, Xnor). A Python or Expression controller is Green whenever it ran. |
| Black | Its state is not one the object is in. |
| White | Ready. |

### Actuators

| Colour | When |
|---|---|
| Red | Any reason under "Every brick". Also an RTX Remix actuator in a scene that does not use Remix. |
| Green | It got an "on" pulse this tic. An RTX Remix actuator is also Green on the tic it sends its setting. |
| Yellow | An Action actuator asked to play while another action holds its layer, or an RTX Remix actuator waiting for Remix to start. |
| Blue | Still at work after its pulse: a motion or servo motion, an action or sound playing, a camera following, a constraint, a steering. |
| Black | No controller of a state the object is in drives it. |
| White | Ready, idle. |

For the tests, the player can write every brick's state at every logic tic
into a file. Set `BGE_REMIX_P10_BRICK_LOG=<file>` to turn it on. Each line
holds the tic, scene, object, brick, kind and state. Unset, it costs
nothing.

## P10.11: states and messages in the Remix View

In the logic layer, each object's bricks stand in columns, one per state.
- **The group**: each column is topped by its state's group ("State 2"). A dashed line runs from each group to every controller in that state.
- **The starting states**: the states the object starts in are marked Green. The live ones come with Run mode.
- **State actuators**: each one is an arrow from the state it is driven in to the state it enters.

In the messages layer, each message's arc now ends in an arrowhead at its
listener. P7's warning for a message nobody hears stays.

**Starting states** (Edit mode): the Starting States panel in the Remix
View's sidebar shows the selected object's starting states, which you can
toggle (states 1 to 30). It also lists which states its bricks use, which
ones State actuators enter, and any state that is never entered.

**A message's subject** (logic editor): beside the Subject field of a
Message sensor or Message actuator there is a small list button. It lists
the subjects the game's bricks already use, so a typing slip cannot break
a message. You can still type a new subject.

## P10.12: two new bricks

**The Timer sensor** (Seconds, Repeat): it fires once, Seconds after its
state becomes active. With Repeat ticked, it fires every Seconds. It counts
the game's time as a timer property does, and needs no property. Each firing
is "on" for one logic tic. Python: `SCA_TimerSensor`, with `seconds` and
`repeat`.

**The RTX Remix actuator** (Setting, Value): on a pulse, it sends one of RTX
Remix's own settings to Remix, as `bge.render.remixSetOption` does.
- **The settings list**: the list button beside Setting holds the Render tab's settings and the quality settings. Picking one fills in its default value. You can also type any `rtx.*` setting.
- **Value**: as Remix writes it: True or False, a number, or "a, b".
- **While Remix starts**: a pulse given while Remix is still starting (about half a minute) is kept and sent on Remix's first frame. The actuator shows Yellow while it waits.
- **In the Classic pipeline**: it does nothing, and the Remix View warns about it (on the Remix layer).
- **Python**: `KX_RemixActuator`, with `setting` and `value`.
- **Settings the game sets itself**: the game sends the camera's effects to Remix itself (vignette, motion blur, bloom, auto exposure) whenever the camera's effects change, and that overrides what an actuator set. The list leaves those settings out.

**Stock 2.79b**: a file using either brick still opens and plays in stock
2.79b, where the bricks do nothing. Stock does not know their type or
data. It reads them without their data, and its converter makes nothing for
them. The rest of the game runs (test T10.c9). If stock 2.79b saves the
file, the two bricks lose their settings. BGE-Remix gives them their
defaults again: 1 second, and no setting.

## P10.13: the bricks' old limits, fixed (approved 6 Nov)

The full list, with each entry marked fixed, option or warning, is in
`bge_remix_p10_brick_limits.md`. Old games play exactly as before. Each
option is kept in the file and is off in every old file, and the files still
open in stock 2.79b, which keeps the options and ignores them.

**Fixed:**
- **A controller in several states**: Shift+click in the logic editor's state menu adds or removes a state. The header shows "2+" for a controller in more than one.
- **Logic Priority**: set per object in the logic editor's sidebar (Logic Order). Objects with a higher number run their controllers first in each logic tic. At 0 (every old file) the order is the one stock 2.79b gives.
- **The Delay sensor**: takes up to 32767 tics in the editor (stock: 5000). The file holds no more than that; for longer waits, use the Timer sensor.
- **The Message sensor's From and Body**: optional filters. When set, the sensor only hears messages from that object, or with that body. Empty means any, as before.

**Options, off unless you turn them on:**
- **Add Once per Message** (Property actuator, Add mode): two messages in one tic add twice, not once.
- **Read Held Keys** (Keyboard sensor): a key already held when the sensor's state is entered fires the sensor once. Stock waits for a new press, unless the sensor's Level is on.
- **Stop Left State Now** (State actuator): the running actuators of the states it leaves stop in the same tic, not one tic later.
- **Own Max Physics Steps** (World tab, Physics): Max Physics Steps works as labelled. Stock reads Max Logic Steps for it. It is on in new files and new scenes, and off in old files.
- **Timers Follow Game Time** (World tab, Physics): when the game runs slower than its logic rate, timers, Timer sensors and added objects' life count the game time each logic step stands for. This only matters with Own Max Physics Steps on: without it, a logic step never stands for more than one tic.

**New warnings in the Remix View's logic layer:**

| Code | Colour | When |
|---|---|---|
| LOG_NO_TARGET | Red (was Black) | An actuator aimed at nothing. |
| DAT_PROP_MISSING | Red (was Black) | A Property sensor watching a property its object does not have. |
| LOG_NO_ACTUATOR | Red | An Actuator sensor watching an actuator its object does not have. |
| LOG_NO_FILE | Red | A Start Game file that is not there. |
| LOG_EXPR_FULL | Note | An expression that fills its 127 characters. |
| LOG_EXPR_NAME_CLASH | Note | An expression name that is both a linked sensor and a property (the sensor is read), or a sensor not linked to the controller (it cannot be read). |
| LOG_SEES_ACTORS | Note | A Near or Radar sensor, and no other object with Actor on. |
| LOG_SHAPE_NO_KEYS | Note | An action that animates shape keys, none of them the mesh's. |

Bricks are now told apart by kind as well as name, so a sensor and an
actuator with the same name no longer mix up their warnings.

For the tests only: `BGE_REMIX_P10_HOLD_KEY=<key>:<tic>` presses a key (a
digit or a letter) at that logic tic and holds it.
