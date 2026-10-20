@ P5: tap the centre value on a power step to cycle instant / 3 s / 10 s / 30 s power (persisted in RTC backup reg 8).
@ Code for the zero padding at the end of the OS image (0xd03d69d0..). Assembled with clang, checked with Ghidra
@ (ghidra/scripts/PatchCheck.java). Patch lines: python3 tools/asm2patch.py patches/P5-centre-power-mode.s
@ Firmware addresses below are R039. Branch targets are written relative to `cave` so no linker is needed.
@hook 0xd01ab3fe pick_live  H1 Workout_ReadPowerLive: ldr.w r0,[r8,#0xc] -> bl pick_live
@hook 0xd0234d8a on_tap     H2 OnTouchRelease tap path: add.w r3,r4,#0x9c -> bl on_tap
@hook 0xd0234a4a header     H3 LoadIntervalData power case: bl 0xd0234b56 -> bl header
        .syntax unified
        .thumb
        .text
        .equ CAVE, 0xd03d69d0
        .equ BKP8, 0x58004070           @ RTC backup register 8: power mode 0..3
        .equ RUNTIME, 0x3003debc        @ pointer to the workout runtime; +0x60 = step target type
cave:
        .set Ride_GetCurrentFieldData,  cave + (0xd01fa45c - CAVE)
        .set EwIsPointInRect,           cave + (0xd0164a86 - CAVE)
        .set EwNewStringAnsi,           cave + (0xd0105964 - CAVE)
        .set ViewsText_OnSetString,     cave + (0xd0144dd0 - CAVE)
        .set ViewsText_OnSetVisible,    cave + (0xd0144e7c - CAVE)
        .set ViewsImage_OnSetVisible,   cave + (0xd0142fda - CAVE)
        .set ViewsImage_OnSetFrame,     cave + (0xd0142ebe - CAVE)
        .set TouchRelease_exit,         cave + (0xd0234dc8 - CAVE)

@ H1 from Workout_ReadPowerLive 0xd01ab3fe (replaces ldr.w r0,[r8,#0xc]). r4 = destination, r8 = power slot.
@ Only the centre value (runtime+0x24) is smoothed; the call filling runtime+0x4c keeps the instant value.
        .thumb_func
pick_live:
        ldr.w   r0, [r8, #0xc]          @ instant (original instruction)
        ldr     r1, =BKP8
        ldr     r1, [r1]
        ands    r1, r1, #3
        beq     1f
        ldr     r2, =RUNTIME
        ldr     r2, [r2]
        adds    r2, #0x24
        cmp     r2, r4
        bne     1f
        push    {r1, lr}
        movs    r0, #4
        bl      Ride_GetCurrentFieldData @ RidePowerData: +4/+8/+0xc = 3/10/30 s average
        pop     {r1, lr}
        ldr.w   r0, [r0, r1, lsl #2]
1:      bx      lr

@ H2 from CourseWorkoutPlay_OnTouchRelease default-tap path 0xd0234d8a (replaces add.w r3,r4,#0x9c). r4 = this.
@ Power step and release point inside the centre box -> next mode, leave through the handler's exit.
        .thumb_func
on_tap:
        push    {r1, lr}
        ldr     r0, =RUNTIME
        ldr     r0, [r0]
        cbz     r0, 3f
        ldrb    r0, [r0, #0x60]
        cmp     r0, #8
        beq     2f
        cmp     r0, #0xc
        beq     2f
        cmp     r0, #0xd
        bne     3f
2:      sub     sp, #8
        movw    r0, #0x2c24             @ this+0x2c24: release point x, y
        add     r0, r4
        ldrd    r0, r1, [r0]
        strd    r0, r1, [sp]
        movw    r0, #0x1790             @ centre box (this+0x1764) bounds x1,y1,x2,y2 at +0x2c
        add     r0, r4
        ldm     r0, {r0, r1, r2, r3}
        bl      EwIsPointInRect
        add     sp, #8
        cbz     r0, 3f
        ldr     r1, =BKP8
        ldr     r0, [r1]
        adds    r0, #1
        and     r0, r0, #3
        str     r0, [r1]
        pop     {r1, lr}
        b.w     TouchRelease_exit
3:      pop     {r1, lr}
        add.w   r3, r4, #0x9c           @ original instruction
        bx      lr

@ H3 from CourseWorkoutPlay_LoadIntervalData power case 0xd0234a4a (replaces bl 0xd0234b56 = icon frame 3).
@ Instant: unchanged. Otherwise hide the icon (this+0x19d0) and show "3s"/"10s"/"30s" in the header text (this+0x1ab8).
        .thumb_func
header:
        ldr     r1, =BKP8
        ldr     r1, [r1]
        ands    r1, r1, #3
        bne     4f
        movs    r1, #3
        movw    r0, #0x19d0
        add     r0, r4
        b.w     ViewsImage_OnSetFrame   @ original behaviour, tail call
4:      push    {r5, lr}
        adr     r5, labels
        add.w   r5, r5, r1, lsl #2
        movw    r0, #0x19d0
        add     r0, r4
        movs    r1, #0
        bl      ViewsImage_OnSetVisible
        mov     r0, r5
        bl      EwNewStringAnsi
        mov     r1, r0
        movw    r0, #0x1ab8
        add     r0, r4
        bl      ViewsText_OnSetString
        movw    r0, #0x1ab8
        add     r0, r4
        movs    r1, #1
        bl      ViewsText_OnSetVisible
        pop     {r5, pc}

        .ltorg
        .balign 4
labels: .asciz  ""
        .balign 4
        .asciz  "3s"
        .balign 4
        .asciz  "10s"
        .balign 4
        .asciz  "30s"
        .balign 4
