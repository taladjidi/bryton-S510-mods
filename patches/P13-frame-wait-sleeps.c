// P13, frame part (was P16): after sending each frame to the screen, the UI task sleeps its 50 ms instead of
// spinning. EwGfxEndUpdate -> Lcd_WaitAndFlush -> Lcd_FlushAndWait: Lcd_FlushFramebuffer (LTDC transfer of the
// 150 KB frame), then HAL_Delay(50), a busy loop on HAL_GetTick. While it spins, no lower priority task runs: the
// Nordic task (BLE and the ANT messages, Di2 included), map tiles, route, ... (1 ms sampling of the UI task: 31 % of
// its time in HAL_Delay / HAL_GetTick). OSTimeDly(50) keeps the same 50 ms for the UI task and gives them the CPU.
// Only in the UI task with the RTOS running (OSPrioCur 12, OSTCBCur set; OSTimeDly does not check that the OS has
// started); anywhere else the stock HAL_Delay.
//
// Build: python3 tools/asm2patch.py patches/P13-frame-wait-sleeps.c (the debug UART part of P13 is plain bytes in
// P13-no-busy-waits.patch)
// @cave 0xd03e0000          after the stock image (build.py grows the image, see GROW_END)
// @hook 0xd02434fa frame_wait  Lcd_FlushAndWait: bl HAL_Delay_veneer(50) -> frame_wait(50)
// @sym HAL_Delay_veneer 0xd0189678
// @sym OSTimeDly 0xd020cc48
#include <stdint.h>

extern void HAL_Delay_veneer(uint32_t ms);
extern void OSTimeDly(uint32_t ticks);

#define OS_PRIO_CUR (*(volatile uint8_t *)0x3003ce90)
#define OS_TCB_CUR (*(void *volatile *)0x3003ceb0)
#define UI_PRIO 12

void frame_wait(uint32_t ms)
{
    if (OS_PRIO_CUR == UI_PRIO && OS_TCB_CUR)
        OSTimeDly(ms);           // 1 tick = 1 ms
    else
        HAL_Delay_veneer(ms);
}
