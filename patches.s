// Extra Buttons patches, appended to the randomizer patches.s by apply_extra_buttons.py.
// Each section is placed at its game address by the linker entries the script adds.

.section .patch_FilterTouch
bl hook_FilterTouch

.section .patch_ItemAssignXDestination1
    bl hook_ItemAssignXDestination

.section .patch_ItemAssignXPosition1
    bl hook_ItemAssignXPositionR0

.section .patch_ItemAssignIPosition1
    bl hook_ItemAssignIPositionR0

.section .patch_ItemAssignYDestination1
    bl hook_ItemAssignYDestination

.section .patch_ItemAssignYPosition1
    bl hook_ItemAssignYPositionR0

.section .patch_ItemAssignIIPosition1
    bl hook_ItemAssignIIPositionR0

.section .patch_ItemAssignXDestination2
    bl hook_ItemAssignXDestination

.section .patch_ItemAssignXPosition2
    bl hook_ItemAssignXPositionR1

.section .patch_ItemAssignIPosition2
    bl hook_ItemAssignIPositionR2

.section .patch_ItemAssignYDestination2
    bl hook_ItemAssignYDestination

.section .patch_ItemAssignYPosition2
    bl hook_ItemAssignYPositionR1

.section .patch_ItemAssignIIPosition2
    bl hook_ItemAssignIIPositionR2

.section .patch_MenuButtons
    b ItemButtons_MenuButtons

.section .patch_ItemsPageActivated
    b hook_ItemsPageActivated

.section .patch_GearPageActivated
    b hook_GearPageActivated

.section .patch_MenuRepeat
    b ItemButtons_MenuRepeat

// Top-screen D-pad display (see item_buttons.c). The game's own code is left
// in place: the boards are built in a slot that is a nop in the original, and
// each draw hook runs the instruction it replaces.
.section .patch_DpadHudBuildBoards
    bl ItemButtons_BuildBoards

.section .patch_DpadHudDraw1
    bl hook_DpadHudDraw1

.section .patch_DpadHudDraw2
    bl hook_DpadHudDraw2
