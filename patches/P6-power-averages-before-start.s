@ P6: compute the 3 s / 10 s / 30 s power averages (and their %FTP) also when no ride is recording, so those
@ fields move before the ride starts (e.g. no GPS fix yet), while paused and after stopping. Fixes BUGS B1.
@ Stock RidePower_Tick (0xd02809f4) returns at once unless the record state is 1; P6 removes that early exit
@ (0xd0280a06, in P6-power-averages-before-start.patch) and re-checks the state right after the averages, so
@ TSS, IF, NP, W'bal and everything recorded stay exactly as stock.
@ Patch lines: python3 tools/asm2patch.py patches/P6-power-averages-before-start.s
@hook 0xd0280a9a p6_gate  RidePower_Tick: bl Ride_UpdateTss -> bl p6_gate (the record-state check, moved below the averages)
        .syntax unified
        .thumb
        .text
        .equ CAVE, 0xd03d6ac0           @ after P5's code (ends 0xd03d6ab4), so both can be installed
cave:
        .set Ride_UpdateTss, cave + (0xd0281854 - CAVE)
        .set Tick_exit,      cave + (0xd0280bbe - CAVE)  @ RidePower_Tick's common exit (pops pc)

@ r5 = &record state (loaded at 0xd02809fe, unchanged until 0xd0280b36). Ride_UpdateTss takes no register
@ arguments, so r0 is free. Not recording: leave RidePower_Tick through its own exit (stack untouched).
        .thumb_func
p6_gate:
        ldrb    r0, [r5]
        cmp     r0, #1
        beq     1f
        b.w     Tick_exit               @ conditional b.w only reaches +-1 MB, the exit is ~1.4 MB away
1:      b.w     Ride_UpdateTss          @ recording: tail call, returns to 0xd0280a9e as stock
