// Extra Buttons hooks, appended to the randomizer hooks.s by apply_extra_buttons.py.

.global hook_FilterTouch
hook_FilterTouch:
    push {r0-r12, lr}
    mov r0, r4
    add r1, sp, #64
    bl ItemButtons_FilterTouch
    pop {r0-r12, lr}
    b 0x2FA404

.global hook_ItemAssignXDestination
hook_ItemAssignXDestination:
    ldr r0, =gItemAssignmentDestination
    ldrb r0, [r0]
    cmp r0, #5
    movne r0, #0xB
    bx lr

.global hook_ItemAssignYDestination
hook_ItemAssignYDestination:
    ldr r0, =gItemAssignmentDestination
    ldrb r0, [r0]
    cmp r0, #0x17
    movne r0, #0x11
    bx lr

.global hook_ItemAssignXPositionR0
hook_ItemAssignXPositionR0:
    cmp r0, #5
    moveq r0, #0x100
    addeq r0, r0, #0xF
    addne r0, r0, #0x108
    bx lr

.global hook_ItemAssignIPositionR0
hook_ItemAssignIPositionR0:
    ldr r0, =gItemAssignmentDestination
    ldrb r0, [r0]
    cmp r0, #5
    moveq r0, #7
    movne r0, #0x49
    bx lr

.global hook_ItemAssignXPositionR1
hook_ItemAssignXPositionR1:
    cmp r0, #5
    moveq r1, #0x100
    addeq r1, r1, #0xF
    addne r1, r0, #0x108
    bx lr

.global hook_ItemAssignIPositionR2
hook_ItemAssignIPositionR2:
    ldr r2, =gItemAssignmentDestination
    ldrb r2, [r2]
    cmp r2, #5
    moveq r2, #7
    movne r2, #0x49
    bx lr

.global hook_ItemAssignYPositionR0
hook_ItemAssignYPositionR0:
    cmp r0, #0x17
    moveq r0, #0x100
    addeq r0, r0, #0xF
    addne r0, r0, #0xF6
    bx lr

.global hook_ItemAssignIIPositionR0
hook_ItemAssignIIPositionR0:
    ldr r0, =gItemAssignmentDestination
    ldrb r0, [r0]
    cmp r0, #0x17
    moveq r0, #0xBF
    movne r0, #0x7D
    bx lr

.global hook_ItemAssignYPositionR1
hook_ItemAssignYPositionR1:
    cmp r0, #0x17
    moveq r1, #0x100
    addeq r1, r1, #0xF
    addne r1, r0, #0xF6
    bx lr

.global hook_ItemAssignIIPositionR2
hook_ItemAssignIIPositionR2:
    ldr r2, =gItemAssignmentDestination
    ldrb r2, [r2]
    cmp r2, #0x17
    moveq r2, #0xBF
    movne r2, #0x7D
    bx lr

.global hook_ItemsPageActivated
hook_ItemsPageActivated:
    push {r0-r12, lr}
    mov r0, #1
    bl ItemButtons_OnPausePage
    pop {r0-r12, lr}
    push {r4, r5, r6, lr}
    b 0x002F8A38

.global hook_GearPageActivated
hook_GearPageActivated:
    push {r0-r12, lr}
    mov r0, #2
    bl ItemButtons_OnPausePage
    pop {r0-r12, lr}
    push {r4-r11, lr}
    b 0x002F0448

// Draws the D-pad boards after the game's own top-screen HUD, once per eye.
// Each hook replaces one instruction at the point where every path of the
// game's HUD code rejoins (including the path taken while a menu is open),
// then runs that instruction itself.
.global hook_DpadHudDraw1
hook_DpadHudDraw1:
    push {r0-r3, r12, lr}
    bl ItemButtons_DrawBoards
    pop {r0-r3, r12, lr}
    ldr r0, [r9]
    bx lr

.global hook_DpadHudDraw2
hook_DpadHudDraw2:
    push {r0-r3, r12, lr}
    bl ItemButtons_DrawBoards
    pop {r0-r3, r12, lr}
    ldr r0, [r7, #0x38]
    bx lr
