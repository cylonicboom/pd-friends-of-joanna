# Character bios

Source of truth for every `struct chrbio` entry the port compiles.
Edit here, then run `tools/mkchrbios build` to write it into
`src/game/training.c`. Never edit the generated region in the C by hand.

These are the built-in bios: the vanilla roster, all `lang`. A bio a mod
supplies lives in that mod (a `ChrBio` block in its modconfig, generated
from the mod's own `chrbios.md` by `tools/mkchrbios build-mod`) and takes
precedence over the one here for the same head.

Two kinds of entry:

- **literal** — the text is authored in this document and compiled in as a
  C string literal.
- **lang** — the text lives in the ROM's own language bank and is *not*
  copied here; only the four `L_*` symbol names are recorded, so the game
  keeps showing it in the player's language.

The field list is the run of `- **key** `value`` lines directly under a
`##` heading. It ends at the first line that is not one, and everything
after that is body — so a line in prose that looks like a field stays
prose.

Inside a bio, `###` becomes a `|Heading -` section and everything else is
emitted verbatim — **a line break in this document is a line break in the
game**, so keep a paragraph on one line unless you mean to wrap it.

---

## Joanna Dark

- **index** `0`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[0]`
- **flags** `0`
- **strings** `L_MISC_219, L_MISC_220, L_MISC_221, L_MISC_222`

_Text lives in the language bank; not duplicated here._

---

## Jonathan

- **index** `1`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[1]`
- **flags** `0`
- **strings** `L_MISC_223, L_MISC_224, L_MISC_225, L_MISC_226`

_Text lives in the language bank; not duplicated here._

---

## Daniel Carrington

- **index** `2`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[2]`
- **flags** `0`
- **strings** `L_MISC_227, L_MISC_228, L_MISC_229, L_MISC_230`

_Text lives in the language bank; not duplicated here._

---

## Cassandra De Vries

- **index** `3`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[3]`
- **flags** `0`
- **strings** `L_MISC_231, L_MISC_232, L_MISC_233, L_MISC_234`

_Text lives in the language bank; not duplicated here._

---

## Trent Easton

- **index** `4`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[4]`
- **flags** `0`
- **strings** `L_MISC_235, L_MISC_236, L_MISC_237, L_MISC_238`

_Text lives in the language bank; not duplicated here._

---

## Dr. Caroll

- **index** `5`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[5]`
- **flags** `0`
- **strings** `L_MISC_239, L_MISC_240, L_MISC_241, L_MISC_242`

_Text lives in the language bank; not duplicated here._

---

## Elvis

- **index** `6`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[6]`
- **flags** `0`
- **strings** `L_MISC_243, L_MISC_244, L_MISC_245, L_MISC_246`

_Text lives in the language bank; not duplicated here._

---

## Mr. Blonde

- **index** `7`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[7]`
- **flags** `0`
- **strings** `L_MISC_247, L_MISC_248, L_MISC_249, L_MISC_250`

_Text lives in the language bank; not duplicated here._

---

## Mr. Blonde (repeat)

- **index** `8`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[8]`
- **flags** `0`
- **strings** `L_MISC_251, L_MISC_252, L_MISC_253, L_MISC_254`

_Text lives in the language bank; not duplicated here._

---

## The U.S. President

- **index** `9`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[9]`
- **flags** `0`
- **strings** `L_MISC_255, L_MISC_256, L_MISC_257, L_MISC_258`

_Text lives in the language bank; not duplicated here._
