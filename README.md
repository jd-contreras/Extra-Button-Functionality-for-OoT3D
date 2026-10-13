# Extra Button Functionality for OoT3D

A patch for the [OoT3D Randomizer](https://github.com/gamestabled/OoT3D_Randomizer) that adds item
buttons to _The Legend of Zelda: Ocarina of Time 3D_.

This repository is for patching a randomizer seed with the added buttons. It is not for building
or installing the randomizer app, and it does not contain the randomizer. To get the randomizer
itself, go to the [official OoT3D Randomizer repository](https://github.com/gamestabled/OoT3D_Randomizer).

## What it adds

- **ZL** uses Item I and **ZR** uses Item II.
- **D-pad Left, Down, Up and Right** are four more item slots. They reset when the game restarts.
- To assign: open the Items page, hover an item, and press ZL, ZR or a D-pad direction.
- Holding a button keeps the item in use, so bows and hookshots charge and release.
- The D-pad items are shown at the top left of the top screen.
- An item the current age can't equip can't be put on the D-pad.

ZL and ZR need a New 3DS / New 2DS, or Citra.

## Installation

The patch is combined with your seed's `code.ips`. The
[OoT3D Model Importer](https://jd-contreras.github.io/oot3d-zobj-import/) page does this in your
browser:

1. Make a seed with the official randomizer, as usual.
2. Open the [OoT3D Model Importer](https://jd-contreras.github.io/oot3d-zobj-import/) and add the
   seed's `code.ips`. A custom model is optional.
3. Choose Citra or 3DS and your game region, press **Convert**, and download the zip.
4. Extract the zip over your seed's files, replacing `code.ips`.
   - **Citra:** into Citra's `load/mods` folder.
   - **3DS:** into your Luma `luma/titles` folder.

### Supported seeds

A seed only fits the randomizer version that made it, so there is one patch per version:

| Randomizer version | USA | Europe |
| --- | --- | --- |
| v4.0.2 | yes | yes (not tested yet) |
| Nightly `6bd4b80` | yes | yes |

Seeds for Citra and for a console both work. A seed from another randomizer version is refused
with a message.

## Using the code

The code is here for anyone to use and modify: add it to official randomizer builds, port it to
another randomizer version, or reuse it in other projects.

Everything in this repository is Extra Buttons' own:

| File | What it is |
| --- | --- |
| `item_buttons.c` | All of the logic: reading the buttons, assigning, using items, the top-screen display |
| `hooks.s` | The small assembly hooks that call into it |
| `patches.s` | The instructions placed at the hooked game addresses |
| `apply_extra_buttons.py` | Adds the three files above to a randomizer checkout, with the list of game addresses |

Extra Buttons is only added on top of the randomizer. Nothing of the randomizer's is replaced.

To build the patch yourself (needs devkitARM):

```
git clone https://github.com/gamestabled/OoT3D_Randomizer
python apply_extra_buttons.py OoT3D_Randomizer
cd OoT3D_Randomizer/code
make REGION=USA      # or REGION=EUR
```

Check out the randomizer version you want first (`v4.0.2` and nightly `6bd4b80` are tested).
Working notes are in [NOTES.md](NOTES.md).

## Credits and license

- Built for the OoT3D Randomizer by gamestabled and contributors (MIT).
- Hook layout and item-assignment hooks adapted from Ocarina Reframed by OTPR26
  (GPL-3.0, https://github.com/OTPR26/OOT3DHud). Because of that, this code is distributed under
  GPL-3.0 as well (see `LICENSE`).
- Nintendo game data is not included. You need your own copy of the game.
