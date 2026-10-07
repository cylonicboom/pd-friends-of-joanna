# Keyboard + mouse player

Which player the keyboard and mouse drive. Default is player 1, as it always
was; it can be handed to any seat from fojOS and the choice is saved.

All of it is `generic` seam: port input and engine gates, nothing
fanfic-coupled, upstreamable as-is.

| Chunk | Commit |
|---|---|
| keyboard+mouse follows `Input.KbmPlayer` | `6f9eb2581` |
| fojos: kb+m column and bar commands | `b6a4bf2da` |

## Using it

- **Players window** (`` `players `` in the awesome bar): the `kb+m` column has
  a radio button on every seated row. Click one to give that player the
  keyboard and mouse.
- **Awesome bar** (Ctrl+P), `>` commands:
  - `Pass KB+M to next player` — the next seated player after the current owner
  - `KB+M to player 1` … `KB+M to player 4` — `kbm2` finds the second

Either one writes `Input.KbmPlayer` to `pd.ini` straight away.

## The rule

**Saved seat if it is seated, otherwise the next seated player after it.**

`Input.KbmPlayer` (0-based, `0..MAXCONTROLLERS-1`) is what was picked.
`inputKbmPlayer()` is who actually has it right now: that seat if
`g_Vars.players[seat]` exists, else the next one that does, wrapping;
0 when nobody is seated.

The saved value is never rewritten by a drop. So:

- a player who drops and comes back gets the keyboard back
- the title screen and menus, where only player 1 is seated, always land on
  player 1, so the menus are never left without a mouse
- in the Players window a free seat that holds the saved value shows `saved`,
  and the stand-in's tooltip says why they have it

## What moves

### Keyboard and mouse buttons — `inputBindPressed` (input.c)

Binds are per pad: `inputReadController(i)` checks `binds[i]`. The keyboard
only ever worked for player 1 because its keys are bound in `binds[0]`.

Now, with `kbmpad = inputKbmPad()`:

- reading pad 0 while `kbmpad != 0`: keyboard and mouse VKs in `binds[0]` are
  skipped
- reading `kbmpad`: those same VKs from `binds[0]` are counted for it

**Keyboard and mouse binds always come from player 1's bind set**; whoever owns
them uses player 1's layout. Gamepad binds are untouched and stay per-seat, so
player 1 keeps their own pad while someone else has the keyboard.

Keyboard keys bound directly in another player's set (`binds[1..]`) still count
for that player, owner or not. Nothing that works today breaks, and that is
the slot separate keyboards and mice per player (multi-USB) would fill later.

"Keyboard or mouse VK" is `vk < VK_JOY_BEGIN` — `VK_KEYBOARD_BEGIN` is 0, so
the lower bound would be an always-true unsigned compare.

### Seat → pad — `hotjoinSeatPad` (hotjoin.c)

The keyboard follows the pad the seat reads, not the seat number:
`optionsGetContpadNum1(g_Vars.playerstats[seat].mpindex)`. Seat 0 always
answers pad 0, unconditionally, so the default path is exactly what it was
before.

### Mouse look and menu mouse

Every site that was hardwired to player 0 now asks `inputKbmPlayer()`:

| Site | What |
|---|---|
| `bondmove.c` `allowmlook` | mouse look on foot |
| `player.c` mouse control | mouse look in the fixed-view / camera path |
| `bondeyespy.c` | camspy / eyespy |
| `activemenutick.c` | the radial (active) menu cursor |
| `menu.c` `menu->playernum` | menu cursor, click, ESC-as-back |

Left alone on purpose: `possess.c` (follows whoever is possessing) and the
time-stop look in `lv.c` (not player-gated at all).

## API

```c
s32  inputKbmGetSaved(void);      // Input.KbmPlayer, as picked
void inputKbmSetSaved(s32 seat);  // out of range -> 0; caller saves the config
s32  inputKbmPlayer(void);        // the seat that has it now
s32  inputKbmPad(void);           // the pad that seat reads

s32  hotjoinKbmSeat(s32 want);    // want if seated, else next seated, else 0
s32  hotjoinSeatPad(s32 seat);    // seat -> contpad1; seat 0 -> 0
```

input.c reaches into the game for seat liveness through the two `hotjoin*`
functions, declared `extern` at the top of input.c, rather than including game
headers into the input layer.

## Not verified yet

- Built and linked by buildq (`b6a4bf2da` PASS); **not run**.
- **Split-screen MP menus.** `menu->playernum` is join order in the MP menus
  (`menutick.c:402`), not necessarily the seat number. Coop should agree; a
  four-player MP setup where seats joined out of order may give the menu
  mouse to the wrong pane.
- The bar has five fixed rows rather than an argument stage. The stage
  today only takes anchors (somewhere to go — `kFojoArgAnchor`), so a seat
  picker there needs a new argument kind. Revisit if seats ever exceed four.
