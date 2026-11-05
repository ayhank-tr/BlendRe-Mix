# P10, third track: the logic bricks brought up to date (P10.10-P10.12)

The bricks stay, and old games play as before. Node logic is not part of
P10. The list of the bricks' old limits (P10.13) is in
`bge_remix_p10_brick_limits.md`, for your approval. Nothing in it has been
fixed yet.

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
