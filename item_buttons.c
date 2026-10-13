// Item buttons: ZL uses Item I and ZR uses Item II (New 3DS only). The D-pad
// Left, Down, Up, and Right use four more item slots that are kept in memory
// only, so they reset on restart and never touch the save.
//
// Assigning: on the Items page, hover an item and press ZL, ZR, or a D-pad
// direction. ZL and ZR press the game's own X and Y, which the game already
// assigns to I and II (saved). A D-pad press copies the hovered item into that
// D-pad slot (not saved). Assignment runs in the per-frame update, which also
// runs on the pause screen.
//
// Using: a press starts a touch on the gameplay HUD's I slot, the same way the
// touchscreen does. The touch stays down while the button is held, so bows and
// hookshots charge and release like they do with a finger on the screen.
// The touch is added where the game samples the touchscreen (ItemButtons_
// FilterTouch, called from the hook in asm/hooks.s).
//
// The game uses whatever item is in its I slot. For a D-pad slot we put that
// item into the I slot while the D-pad is held, then restore the saved I item.
#include "z3D/z3D.h"
#include "hid.h"
#include "input.h"
#include "common.h"
#include "3ds/services/irrst.h"

#define ITEM_BUTTON_ZL (1 << 14)
#define ITEM_BUTTON_ZR (1 << 15)

// Gameplay HUD touch points for the I and II item slots.
#define ITEM_I_TOUCH_X 316
#define ITEM_I_TOUCH_Y 3
#define ITEM_II_TOUCH_X 318
#define ITEM_II_TOUCH_Y 238

// Minimum frames the touch stays down, so a quick tap is never missed.
#define ITEM_TOUCH_MIN_FRAMES 3

// The game's empty item marker.
#define ITEM_DPAD_EMPTY 0xFF

// Index into sDpadItems, in the order Left, Down, Up, Right.
#define DPAD_SLOT_LEFT 0
#define DPAD_SLOT_DOWN 1
#define DPAD_SLOT_UP 2
#define DPAD_SLOT_RIGHT 3
#define DPAD_SLOT_COUNT 4

// Native assignment destinations, read by the hooks in asm/hooks.s.
#define ASSIGN_DEST_I 5
#define ASSIGN_DEST_II 0x17

#define ITEM_SLOT_NONE 0
#define ITEM_SLOT_I 1
#define ITEM_SLOT_II 2
#define ITEM_SLOT_DPAD 3

// Native assignment destination for this frame only (reset in ItemButtons_Update).
u8 gItemAssignmentDestination = 0;

// Session-only D-pad slots (not saved). Each remembers the inventory slot it
// was assigned from. Its item is re-read from the inventory every frame, so an
// emptied bottle or a used-up item is reflected at once.
#define INV_SLOT_NONE 0xFF
static u8 sDpadInvSlots[DPAD_SLOT_COUNT] = { INV_SLOT_NONE, INV_SLOT_NONE, INV_SLOT_NONE, INV_SLOT_NONE };
static u8 sDpadItems[DPAD_SLOT_COUNT] = { ITEM_DPAD_EMPTY, ITEM_DPAD_EMPTY, ITEM_DPAD_EMPTY, ITEM_DPAD_EMPTY };

// The game's own per-item age table: 0 = adult only, 1 = child only, 9 = both.
// The Items page greys items out from it. The randomizer's "as child" and "as
// adult" settings edit this same table, so they are respected here too.
#define ITEM_USABLE_BY_BOTH 9
static u8 ItemUsableNow(u8 item) {
    if (item == ITEM_DPAD_EMPTY) return 0;
    const u8 requirement = gItemUsabilityTable[item];
    return requirement == ITEM_USABLE_BY_BOTH || requirement == gSaveContext.linkAge;
}

// A slot whose item the current age can't use counts as empty: it can't be
// used and isn't shown. It comes back when Link is the right age again.
static void RefreshDpadItems(void) {
    for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) {
        const u8 invSlot = sDpadInvSlots[slot];
        const u8 item = invSlot < sizeof(gSaveContext.items) ? gSaveContext.items[invSlot] : ITEM_DPAD_EMPTY;
        sDpadItems[slot] = ItemUsableNow(item) ? item : ITEM_DPAD_EMPTY;
    }
}

static const u32 sDpadButtons[DPAD_SLOT_COUNT] = { BUTTON_LEFT, BUTTON_DOWN, BUTTON_UP, BUTTON_RIGHT };

#define DPAD_BUTTONS_MASK (BUTTON_LEFT | BUTTON_DOWN | BUTTON_UP | BUTTON_RIGHT)

// Set by the native page hooks (asm/hooks.s): 1 = Items, 2 = Gear.
static u8 sPausePage = 0;

// The Items page cursor: column and row in the game's Items manager, the same
// fields its own assignment code reads at 0x2EC508. The page is a grid of six
// columns and four rows. The last column holds the I, X, Y, and II buttons.
#define ITEMS_MANAGER 0x005066F8
#define ITEMS_CURSOR_COLUMN (*(volatile s32*)(ITEMS_MANAGER + 0x78))
#define ITEMS_CURSOR_ROW (*(volatile s32*)(ITEMS_MANAGER + 0x7C))
#define ITEMS_GRID_COLUMNS 6
#define ITEMS_GRID_ROWS 4

// The inventory slot under the Items page cursor, or INV_SLOT_NONE. The game
// keeps a per-age table from grid cell to inventory slot.
static u8 HoveredInventorySlot(void) {
    const s32 column = ITEMS_CURSOR_COLUMN;
    const s32 row = ITEMS_CURSOR_ROW;
    // The button column isn't an item.
    if (column < 0 || column >= ITEMS_GRID_COLUMNS - 1 || row < 0 || row >= ITEMS_GRID_ROWS) return INV_SLOT_NONE;
    const s32 cell = column + row * ITEMS_GRID_COLUMNS;
    const u8 invSlot = gSaveContext.linkAge ? gSaveContext.itemMenuChild[cell] : gSaveContext.itemMenuAdult[cell];
    return invSlot < sizeof(gSaveContext.items) ? invSlot : INV_SLOT_NONE;
}

// The X or Y press injected for one frame by the assignment code.
static u32 sInjectedAssign = 0;


static u32 sPreviousIrrstHeld = 0;
static u8 sActiveSlot = ITEM_SLOT_NONE;
static u8 sActiveDpad = 0;
static u8 sMinFramesLeft = 0;
static int16_t sTouchX = 0;
static int16_t sTouchY = 0;

// The I item and inventory slot saved while a D-pad item sits in I, and
// whether we swapped them.
static u8 sSavedItemI = ITEM_DPAD_EMPTY;
static u8 sSavedSlotI = INV_SLOT_NONE;
static u8 sSwappedI = 0;

// The D-pad slot whose item Link is holding, or DPAD_HELD_NONE. The game puts
// a held item away as soon as it isn't on any of its item buttons, so that
// item stays in I for as long as it's in Link's hand.
#define DPAD_HELD_NONE 0xFF
#define ITEM_HELD_WAIT_FRAMES 20
static u8 sHeldDpad = DPAD_HELD_NONE;
// Frames left to wait for the item to reach Link's hand after the press.
static u8 sHeldWait = 0;

static void RestoreItemI(void) {
    if (sSwappedI) {
        gSaveContext.equips.buttonItems[3] = sSavedItemI;
        gSaveContext.equips.buttonSlots[2] = sSavedSlotI;
        sSwappedI = 0;
    }
}

// Puts a D-pad slot's item into the I slot, keeping the original to restore.
static void SwapIntoItemI(u8 slot) {
    if (!sSwappedI) {
        sSavedItemI = gSaveContext.equips.buttonItems[3];
        sSavedSlotI = gSaveContext.equips.buttonSlots[2];
        sSwappedI = 1;
    }
    gSaveContext.equips.buttonItems[3] = sDpadItems[slot];
    gSaveContext.equips.buttonSlots[2] = sDpadInvSlots[slot];
}

static void BeginDpadSlot(u8 slot) {
    sActiveSlot = ITEM_SLOT_DPAD;
    sActiveDpad = slot;
    sTouchX = ITEM_I_TOUCH_X;
    sTouchY = ITEM_I_TOUCH_Y;
    sMinFramesLeft = ITEM_TOUCH_MIN_FRAMES;
    sHeldDpad = DPAD_HELD_NONE;
    SwapIntoItemI(slot);
}

// True while Link is switching to, or using, an item that isn't the one in his
// hand, for example drinking from a bottle. When that finishes, the game writes
// the result (such as the emptied bottle) through the I slot it was started
// from, so the D-pad item has to stay in I until then. Restoring I early makes
// that write land on the real I item instead.
static u8 PlayerItemActionPending(void) {
    const Player* const player = PLAYER;
    return player != 0 && player->itemActionParam != player->heldItemActionParam;
}

// True when a D-pad item is in I and the game may still write through I.
static u8 HeldDpadItemBusy(void) {
    return sHeldDpad != DPAD_HELD_NONE && PlayerItemActionPending();
}

#define ITEM_ID_LENS 0x0F
#define ITEM_ID_MASK_FIRST 0x24
#define ITEM_ID_MASK_LAST 0x2B

// True while a D-pad item has an ongoing effect that the game ends as soon as
// the item isn't on one of its item buttons: the Lens of Truth while it's on,
// or a mask while it's worn.
static u8 DpadItemEffectActive(u8 item, const Player* player) {
    if (item == ITEM_ID_LENS) return gGlobalContext->actorCtx.unk_03 != 0; // lens active
    if (item >= ITEM_ID_MASK_FIRST && item <= ITEM_ID_MASK_LAST) return player != 0 && player->currentMask != 0;
    return 0;
}

// Keeps a held D-pad item in I. Returns 1 while it should stay there.
static u8 UpdateHeldDpadItem(void) {
    if (sHeldDpad == DPAD_HELD_NONE) return 0;

    const Player* const player = PLAYER;
    const u8 item = sDpadItems[sHeldDpad];
    const u8 inHand = player != 0 && item != ITEM_DPAD_EMPTY && player->heldItemId == item;
    if (inHand || PlayerItemActionPending() || DpadItemEffectActive(item, player)) {
        sHeldWait = 0;
    } else if (sHeldWait > 0) {
        sHeldWait--;
    } else {
        sHeldDpad = DPAD_HELD_NONE;
        return 0;
    }
    SwapIntoItemI(sHeldDpad);
    return 1;
}

// The game's own copy of the play pad (inside GlobalContext), read by the pause
// screen. Offsets from the Single build's input_remap.c (USA). Presses are added
// and removed here only: the HID shared memory (real_hid) is read-only on a
// console, and writing to it crashes the game there.
#define PLAY_PAD_HELD_OFFSET 0x14
#define PLAY_PAD_PRESSED_OFFSET 0x18
#define PLAY_PAD_RELEASED_OFFSET 0x1C

static u32 PlayPadPressedBits(GlobalContext* g) {
    return *(volatile u32*)((u8*)g + PLAY_PAD_PRESSED_OFFSET);
}

static void PlayPadAdd(GlobalContext* g, u32 button) {
    volatile u32* const held = (volatile u32*)((u8*)g + PLAY_PAD_HELD_OFFSET);
    volatile u32* const pressed = (volatile u32*)((u8*)g + PLAY_PAD_PRESSED_OFFSET);
    *held |= button;
    *pressed |= button;
}

static void PlayPadClear(GlobalContext* g, u32 button) {
    volatile u32* const held = (volatile u32*)((u8*)g + PLAY_PAD_HELD_OFFSET);
    volatile u32* const pressed = (volatile u32*)((u8*)g + PLAY_PAD_PRESSED_OFFSET);
    *held &= ~button;
    *pressed &= ~button;
}

// On the Items page: ZL presses the game's X (assign to I), ZR its Y (assign
// to II), and a D-pad press puts the hovered item on that D-pad slot.
static void UpdateAssignment(GlobalContext* g, u32 pressed) {
    if (pressed & ITEM_BUTTON_ZL) {
        gItemAssignmentDestination = ASSIGN_DEST_I;
        PlayPadAdd(g, BUTTON_X);
        sInjectedAssign = BUTTON_X;
    } else if (pressed & ITEM_BUTTON_ZR) {
        gItemAssignmentDestination = ASSIGN_DEST_II;
        PlayPadAdd(g, BUTTON_Y);
        sInjectedAssign = BUTTON_Y;
    }

    // A D-pad press puts the hovered item on that D-pad slot. Nothing of the
    // game's is written, so its own item buttons and their icons don't change.
    // The press is removed so the cursor doesn't also move.
    const pad_t* const pad = &real_hid.pad.pads[real_hid.pad.index];
    for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) {
        if ((pad->pressed.val | PlayPadPressedBits(g)) & sDpadButtons[slot]) {
            const u8 invSlot = HoveredInventorySlot();
            // Only an item the Items page lets this age equip can be assigned.
            if (invSlot != INV_SLOT_NONE && ItemUsableNow(gSaveContext.items[invSlot])) {
                // An item sits on one D-pad direction at a time.
                for (u8 other = 0; other < DPAD_SLOT_COUNT; other++) {
                    if (sDpadInvSlots[other] == invSlot) sDpadInvSlots[other] = INV_SLOT_NONE;
                }
                sDpadInvSlots[slot] = invSlot;
            }
            PlayPadClear(g, sDpadButtons[slot]);
        }
    }
}

static u8 ItemsPageOpen(void) {
    return PauseContext_GetState() != 0 && sPausePage == 1;
}

// True while a physical D-pad button is held. The circle pad uses the same
// directional bits, so only mask those when the D-pad itself is down.
static u32 DpadHeldNow(void) {
    return real_hid.pad.pads[real_hid.pad.index].curr.val & DPAD_BUTTONS_MASK;
}

// These two replace the game's menu repeat read (0x33B5D0) and menu-button read
// (0x33B5EC) and return the same values. On the Items page the D-pad assigns,
// so its directions are removed there and the cursor doesn't also move.
u32 ItemButtons_MenuRepeat(void) {
    const volatile u32* const input = (const volatile u32*)0x0050AF0C;
    const u32 repeat = input[0x24/4] ? input[0x24/4] : input[0x20/4];
    return (ItemsPageOpen() && DpadHeldNow()) ? (repeat & ~DPAD_BUTTONS_MASK) : repeat;
}

u32 ItemButtons_MenuButtons(void) {
    const volatile u32* const input = (const volatile u32*)0x0050AF0C;
    const u32 buttons = input[3] ? input[3] : input[2];
    return (ItemsPageOpen() && DpadHeldNow()) ? (buttons & ~DPAD_BUTTONS_MASK) : buttons;
}

void ItemButtons_OnPausePage(u32 page) {
    sPausePage = (u8)page;
}

void ItemButtons_Update(GlobalContext* globalCtx) {
    // Clear the assign press we injected last frame, so the game doesn't assign again.
    if (sInjectedAssign) {
        PlayPadClear(globalCtx, sInjectedAssign);
        sInjectedAssign = 0;
    }

    RefreshDpadItems();

    // Reset each frame. The assignment hooks only read it during the game's
    // Items update, right after this runs.
    gItemAssignmentDestination = 0;

    const u32 held = irrstKeysHeld();
    const u32 pressed = held & ~sPreviousIrrstHeld;
    sPreviousIrrstHeld = held;

    // Pause closed: forget the page. The page hooks set it when a page opens.
    if (PauseContext_GetState() == 0) sPausePage = 0;

    // On the Items page, buttons assign instead of use.
    if (PauseContext_GetState() != 0) {
        // Show the real I item in the menu, unless the game may still write
        // through I for a D-pad item.
        if (!HeldDpadItemBusy()) RestoreItemI();
        sActiveSlot = ITEM_SLOT_NONE;
        sMinFramesLeft = 0;
        if (sPausePage == 1) UpdateAssignment(globalCtx, pressed);
        return;
    }

    // Item use only in normal gameplay (gameMode 0).
    if (gSaveContext.gameMode != 0) {
        RestoreItemI();
        sActiveSlot = ITEM_SLOT_NONE;
        sMinFramesLeft = 0;
        sHeldDpad = DPAD_HELD_NONE;
        return;
    }

    if (sActiveSlot == ITEM_SLOT_NONE) {
        if (pressed & ITEM_BUTTON_ZL) {
            // ZL uses the real I item, so a held D-pad item gives the slot back,
            // unless the game may still write through I for that item.
            if (!HeldDpadItemBusy()) {
                sHeldDpad = DPAD_HELD_NONE;
                RestoreItemI();
            }
            sActiveSlot = ITEM_SLOT_I;
            sTouchX = ITEM_I_TOUCH_X;
            sTouchY = ITEM_I_TOUCH_Y;
            sMinFramesLeft = ITEM_TOUCH_MIN_FRAMES;
        } else if (pressed & ITEM_BUTTON_ZR) {
            sActiveSlot = ITEM_SLOT_II;
            sTouchX = ITEM_II_TOUCH_X;
            sTouchY = ITEM_II_TOUCH_Y;
            sMinFramesLeft = ITEM_TOUCH_MIN_FRAMES;
        } else if (!HeldDpadItemBusy()) {
            // A different D-pad item can't take I while the game may still
            // write through I for the current one.
            for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) {
                if ((rInputCtx.pressed.val & sDpadButtons[slot]) && sDpadItems[slot] != ITEM_DPAD_EMPTY) {
                    BeginDpadSlot(slot);
                    break;
                }
            }
        }
    }

    if (sActiveSlot == ITEM_SLOT_NONE) {
        if (!UpdateHeldDpadItem()) RestoreItemI();
        return;
    }

    // Keep the touch down while its button is still held, and for the minimum
    // frames after a press. Then release it.
    u32 stillHeld;
    if (sActiveSlot == ITEM_SLOT_I) {
        stillHeld = held & ITEM_BUTTON_ZL;
    } else if (sActiveSlot == ITEM_SLOT_II) {
        stillHeld = held & ITEM_BUTTON_ZR;
    } else {
        stillHeld = rInputCtx.cur.val & sDpadButtons[sActiveDpad];
    }
    if (!stillHeld && sMinFramesLeft == 0) {
        if (sActiveSlot == ITEM_SLOT_DPAD) {
            // Leave the item in I while it's in Link's hand (see UpdateHeldDpadItem).
            sHeldDpad = sActiveDpad;
            sHeldWait = ITEM_HELD_WAIT_FRAMES;
        }
        sActiveSlot = ITEM_SLOT_NONE;
        return;
    }
    if (sMinFramesLeft > 0) sMinFramesLeft--;
}

// Called with the native touch sample (two s16 coordinates and a contact byte)
// and its count. While an item button is active, replace the sample with the
// item slot's touch. Otherwise leave the real touchscreen input alone.
void ItemButtons_FilterTouch(void* output, s32* count) {
    if (sActiveSlot == ITEM_SLOT_NONE) return;

    u8* const sample = (u8*)output;
    *(s16*)(sample + 0) = sTouchX;
    *(s16*)(sample + 2) = sTouchY;
    sample[4] = 1;
    *count = 1;
}

// ---------------------------------------------------------------------------
// Top-screen D-pad display: item icons in a cross, with small arrows between.
//
// Two GPU boards are built the same way the game builds its own top-screen
// boards (ItemButtons_BuildBoards runs right after the game's HUD setup) and
// drawn right after the game's own top-screen HUD (ItemButtons_DrawBoards).
// Nothing of the game's is replaced. The art is the game's own:
//   icons:  icon_item_menu00 (menu texture slot 10), a 512x512 item atlas
//   arrows: menu_top_parts00 (menu texture slot 2), 512x256
// The GPU draws them, so the top screen keeps its resolution.
// ---------------------------------------------------------------------------
#define HUD_VERTS (DPAD_SLOT_COUNT * 4)
#define HUD_INDEX_COUNT (4 + (DPAD_SLOT_COUNT - 1) * 6)

// Vertex order per quad: lower-left, lower-right, upper-left, upper-right.
// The top screen is 400x240 with the origin at the top-left (Y grows downward).
static float sIconPositions[HUD_VERTS * 3] __attribute__((aligned(16)));
static float sIconUVs[HUD_VERTS * 2] __attribute__((aligned(16)));
static float sArrowPositions[HUD_VERTS * 3] __attribute__((aligned(16)));
static float sArrowUVs[HUD_VERTS * 2] __attribute__((aligned(16)));
// Four-vertex strips joined with repeated (degenerate) vertices.
static u16 sHudIndices[HUD_INDEX_COUNT] __attribute__((aligned(16))) = {
    0, 1, 2, 3, 3, 4, 4, 5, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 12, 13, 14, 15,
};

// Game functions and data (USA), as used by the game's own board setup at 0x469590.
typedef void (*HudUploadFn)(void* board, u32 byteCount, const void* source);
#define HUD_UPLOAD_UVS ((HudUploadFn)0x00317D1C)
#define HUD_UPLOAD_POSITIONS ((HudUploadFn)0x0036759C)
#define HUD_CONSTRUCT_BOARD ((void* (*)(void* memory, void* descriptor))0x00348F34)
#define HUD_ASSIGN_TEXTURE ((void (*)(void*, u32, void*, u32, u32, u32, u32))0x00348A64)
#define HUD_REGISTER_BOARD ((u32* (*)(void* renderer, void* board, u32, u32))0x0034897C)
#define HUD_GET_MENU_TEXTURE ((void* (*)(u32 slot))0x002E11D0)
#define HUD_DESCRIPTOR_TEMPLATE ((const u32*)0x004D30A4)
#define HUD_DESCRIPTOR_WORDS (0x118 / 4)
#define HUD_BOARD_HEAP (*(void**)0x0055A1F8)
#define HUD_RENDERER (*(void**)(0x005C05B8 + 0x47C))
#define HUD_BOARD_SIZE 0x1B8
#define HUD_TEXTURE_LINEAR 0x2601
#define HUD_TEXTURE_CLAMP 0x812F
#define MENU_TEXTURE_TOP_PARTS 2
#define MENU_TEXTURE_ITEM_ATLAS 10

typedef struct {
    void* board;
    u32* drawable;
} HudBoard;

static HudBoard sIconBoard;
static HudBoard sArrowBoard;
static u8 sHudVisible = 0;

// The item atlas is a 12x12 grid of 42-pixel cells in item-ID order (the last
// 8 pixels on each axis are unused).
#define ATLAS_CELLS 12
#define ATLAS_ITEM_COUNT (ATLAS_CELLS * ATLAS_CELLS)
#define ATLAS_CELL_SIZE (42.0f / 512.0f)

// Icon centers in a cross at the top-left: Left, Down, Up, Right. The cross
// sits DPAD_HUD_PADDING from both edges.
#define DPAD_HUD_HALF 6.75f
#define DPAD_HUD_STEP 15.0f
#define DPAD_HUD_PADDING 8.0f
#define DPAD_HUD_CENTER_X (DPAD_HUD_PADDING + DPAD_HUD_STEP + DPAD_HUD_HALF)
#define DPAD_HUD_CENTER_Y (DPAD_HUD_PADDING + DPAD_HUD_STEP + DPAD_HUD_HALF)

// The yellow up-arrow in menu_top_parts00, in pixels.
#define ARROW_TEX_X0 474.0f
#define ARROW_TEX_X1 486.0f
#define ARROW_TEX_Y0 1.0f
#define ARROW_TEX_Y1 14.0f
// Arrow size and distance from the centre of the cross.
#define ARROW_HALF 3.0f
#define ARROW_OFFSET 5.0f

// What the boards currently show, so they're only uploaded when something changes.
static u8 sIconsShown[DPAD_SLOT_COUNT];
static u8 sIconsDirty = 1;
// Bit N shows the arrow for D-pad slot N. 0xFF is not a valid mask.
static u8 sArrowShownMask = 0xFF;

// Writes one quad's corners. Y grows downward, so the lower edge has the larger Y.
static void SetQuad(float* pos, float x, float y, float half) {
    pos[0] = x - half; pos[1] = y + half;  pos[2] = 0.0f;
    pos[3] = x + half; pos[4] = y + half;  pos[5] = 0.0f;
    pos[6] = x - half; pos[7] = y - half;  pos[8] = 0.0f;
    pos[9] = x + half; pos[10] = y - half; pos[11] = 0.0f;
}

// Slot offsets from the centre of the cross: Left, Down, Up, Right.
static const s8 sSlotDirection[DPAD_SLOT_COUNT][2] = { { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 0 } };

static void BuildIconQuad(u8 slot) {
    float* const uv = sIconUVs + slot * 4 * 2;
    const u8 item = sDpadItems[slot];

    if (item >= ATLAS_ITEM_COUNT) {
        // Empty slot: collapse the quad off-screen.
        SetQuad(sIconPositions + slot * 4 * 3, -100.0f, -100.0f, 0.0f);
        return;
    }
    SetQuad(sIconPositions + slot * 4 * 3, DPAD_HUD_CENTER_X + sSlotDirection[slot][0] * DPAD_HUD_STEP,
            DPAD_HUD_CENTER_Y + sSlotDirection[slot][1] * DPAD_HUD_STEP, DPAD_HUD_HALF);

    // V runs bottom-to-top, so row 0 of the atlas is at V = 1. Inset by half a
    // texel so neighbouring icons don't bleed in.
    const float inset = 0.5f / 512.0f;
    const float u0 = (item % ATLAS_CELLS) * ATLAS_CELL_SIZE + inset;
    const float u1 = (item % ATLAS_CELLS + 1) * ATLAS_CELL_SIZE - inset;
    const float vTop = 1.0f - (item / ATLAS_CELLS) * ATLAS_CELL_SIZE - inset;
    const float vBottom = 1.0f - (item / ATLAS_CELLS + 1) * ATLAS_CELL_SIZE + inset;
    uv[0] = u0; uv[1] = vBottom;
    uv[2] = u1; uv[3] = vBottom;
    uv[4] = u0; uv[5] = vTop;
    uv[6] = u1; uv[7] = vTop;
}

static void BuildArrowQuads(u8 shownMask) {
    // Texture corners (V runs bottom-to-top): top-left, top-right, bottom-left,
    // bottom-right.
    const float u0 = ARROW_TEX_X0 / 512.0f, u1 = ARROW_TEX_X1 / 512.0f;
    const float vT = 1.0f - ARROW_TEX_Y0 / 256.0f, vB = 1.0f - ARROW_TEX_Y1 / 256.0f;
    const float tex[4][2] = { { u0, vT }, { u1, vT }, { u0, vB }, { u1, vB } };
    // Which texture corner goes on each quad vertex (lower-left, lower-right,
    // upper-left, upper-right) so the arrow points Left, Down, Up, Right.
    static const u8 corner[DPAD_SLOT_COUNT][4] = {
        { 0, 2, 1, 3 }, // Left: the arrow's top faces the left edge
        { 1, 0, 3, 2 }, // Down: rotated half a turn
        { 2, 3, 0, 1 }, // Up: as drawn
        { 3, 1, 2, 0 }, // Right: the arrow's top faces the right edge
    };

    for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) {
        float* const pos = sArrowPositions + slot * 4 * 3;
        float* const uv = sArrowUVs + slot * 4 * 2;
        if ((shownMask >> slot) & 1) {
            SetQuad(pos, DPAD_HUD_CENTER_X + sSlotDirection[slot][0] * ARROW_OFFSET,
                    DPAD_HUD_CENTER_Y + sSlotDirection[slot][1] * ARROW_OFFSET, ARROW_HALF);
        } else {
            // A hidden arrow is collapsed off-screen.
            SetQuad(pos, -100.0f, -100.0f, 0.0f);
        }
        for (u32 v = 0; v < 4; v++) {
            uv[v * 2] = tex[corner[slot][v]][0];
            uv[v * 2 + 1] = tex[corner[slot][v]][1];
        }
    }
}

// Builds one board the way the game does at 0x469590: allocate, construct from
// a descriptor, give it a menu texture, and register it with the renderer.
static void BuildBoard(HudBoard* out, float* positions, float* uvs, u32 textureSlot) {
    out->board = 0;
    out->drawable = 0;

    // The game's own 0x118-byte descriptor, with our buffers and counts.
    u32 descriptor[HUD_DESCRIPTOR_WORDS];
    for (u32 i = 0; i < HUD_DESCRIPTOR_WORDS; i++) descriptor[i] = HUD_DESCRIPTOR_TEMPLATE[i];
    descriptor[0] = (u32)positions;
    descriptor[1] = (u32)uvs;
    descriptor[3] = HUD_VERTS;
    descriptor[4] = (u32)sHudIndices;
    descriptor[5] = HUD_INDEX_COUNT;

    void* const heap = HUD_BOARD_HEAP;
    void* const memory = ((void* (*)(void*, u32))(*(u32**)heap)[2])(heap, HUD_BOARD_SIZE);
    if (memory == 0) return;
    void* const board = HUD_CONSTRUCT_BOARD(memory, descriptor);
    HUD_ASSIGN_TEXTURE(board, 0, HUD_GET_MENU_TEXTURE(textureSlot), HUD_TEXTURE_LINEAR, HUD_TEXTURE_LINEAR,
                       HUD_TEXTURE_CLAMP, HUD_TEXTURE_CLAMP);
    u32* const drawable = HUD_REGISTER_BOARD(HUD_RENDERER, board, 0, 0);
    drawable[0x178 / 4] |= 3;

    out->board = board;
    out->drawable = drawable;
}

// Called right after the game builds its own top-screen HUD boards, each time
// it does (see asm/patches.s).
void ItemButtons_BuildBoards(void) {
    sHudVisible = 0;
    for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) BuildIconQuad(slot);
    BuildArrowQuads(0);
    BuildBoard(&sIconBoard, sIconPositions, sIconUVs, MENU_TEXTURE_ITEM_ATLAS);
    BuildBoard(&sArrowBoard, sArrowPositions, sArrowUVs, MENU_TEXTURE_TOP_PARTS);
    sIconsDirty = 1;
    sArrowShownMask = 0xFF;
}

static void DrawBoard(const HudBoard* hudBoard) {
    if (hudBoard->drawable == 0) return;
    ((void (*)(void*))(*(u32**)hudBoard->drawable)[3])(hudBoard->drawable);
}

// Called after the game draws its own top-screen HUD, once per eye, including
// while a menu is open (see asm/hooks.s).
void ItemButtons_DrawBoards(void) {
    if (!sHudVisible) return;
    DrawBoard(&sIconBoard);
    DrawBoard(&sArrowBoard);
}

// Runs after the game's update. Shows the cross while a D-pad slot holds an
// item, and on the Items page even when the slots are empty.
void ItemButtons_UpdateHud(void) {
    u8 anyItem = 0;
    u8 arrowMask = 0;
    const u8 itemsPage = ItemsPageOpen();
    for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) {
        const u8 hasItem = sDpadItems[slot] < ATLAS_ITEM_COUNT;
        if (hasItem) anyItem = 1;
        // An arrow shows for each slot that holds an item. On the Items page
        // all four show, so the cross is visible before anything is assigned.
        if (hasItem || itemsPage) arrowMask |= 1 << slot;
        if (sDpadItems[slot] != sIconsShown[slot]) sIconsDirty = 1;
    }

    const u8 visible = sIconBoard.board != 0 && sArrowBoard.board != 0 && IsInGame() && (anyItem || itemsPage);
    if (visible && sIconsDirty) {
        for (u8 slot = 0; slot < DPAD_SLOT_COUNT; slot++) {
            BuildIconQuad(slot);
            sIconsShown[slot] = sDpadItems[slot];
        }
        HUD_UPLOAD_POSITIONS(sIconBoard.board, sizeof(sIconPositions), sIconPositions);
        HUD_UPLOAD_UVS(sIconBoard.board, sizeof(sIconUVs), sIconUVs);
        sIconsDirty = 0;
    }
    if (visible && arrowMask != sArrowShownMask) {
        BuildArrowQuads(arrowMask);
        HUD_UPLOAD_POSITIONS(sArrowBoard.board, sizeof(sArrowPositions), sArrowPositions);
        sArrowShownMask = arrowMask;
    }
    sHudVisible = visible;
}
