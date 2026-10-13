# Extra Button Functionality for OoT3D: developer notes

The [README](README.md) is the description and installation guide. These are the working notes.

## Behaviour

- **ZL** uses Item I, **ZR** uses Item II (through the game's own I and II slots).
- **D-pad Left, Down, Up, Right** are four more item slots (session only, reset on restart).
- Assign on the Items page: hover an item, then press ZL, ZR, or a D-pad direction.
- Holding a button keeps the item in use (bows and hookshots charge and release).
- The D-pad items show at the top left of the top screen, drawn with the game's own graphics on
  two boards of our own.
- An item the current age can't equip can't be put on the D-pad (read from the game's item
  usability table, so settings that change it are followed).
- What D-pad Down and Right already did in the game and the randomizer is left in place.

## How it sits on the randomizer

`apply_extra_buttons.py` adds, to a randomizer checkout: `item_buttons.c`, our `hooks.s` and
`patches.s` after the randomizer's own, one linker entry per hooked game address, five
declarations in `input.h`, and two calls in `main.c` (`ItemButtons_Update` after `Input_Update`,
`ItemButtons_UpdateHud` at the start of `after_GlobalContext_Update`). From the randomizer it only
uses `irrstKeysHeld` (ZL / ZR), `rInputCtx`, `gGlobalContext` and the game structures in its
headers.

The script handles both file layouts seen so far: v4.0.x (`code/src/hooks.s`, linker entries with
`region_offset`) and the nightly (`code/src/asm/hooks.s`, `_LD_OFF`).

## Status

- USA and Europe, Citra and console. Tested: USA nightly `6bd4b80` (Citra), Europe nightly
  `6bd4b80` (Citra), USA `v4.0.2` (New 2DS XL). Europe `v4.0.2` is built but untested.
- ZL / ZR need a New 3DS / 2DS (or Citra).
- A D-pad slot can't be cleared except by restarting the game.

## Things learned the hard way

- The HID shared memory is read-only on a console: never write to `real_hid` (it crashed there).
  Presses are added and removed in the game's own copy of the pad, inside GlobalContext.
- Europe: only code between 0x41A144 and 0x4A5B00 moves (by 0x24 up to 0x436690, then 0x20).
  Game data and code outside that range sit at the USA addresses.
- An item in hand has to stay on a button or the game puts it away; a bottle's result is written
  through the button it was used from; the Lens of Truth and masks turn off when not on a button.
  The D-pad item is kept in the I slot while any of these is in progress.
- The official randomizer builds come from another compiler, so addresses in a seed's `code.ips`
  never match a local build.

## Publishing a build to the site

The importer's `tools/gen_extra_buttons.py` turns a build into the files the site serves; the
steps are in that repository's `NOTES.md`.

## Later idea

A self-contained add-on appended to any randomizer build's `code.ips`, so no per-version build is
needed. Blocker: ZL / ZR are read through the randomizer's own ir:rst state, whose address changes
per build.
