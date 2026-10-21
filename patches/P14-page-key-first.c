// P14: the unit's page button asks for the next page before the key click, not after it. Stock, the key task
// (HRApp_ProcessKeyMessage) first runs the key listeners, which sleep 50 ms and play an 80 ms blocking beep, and
// only then Ride_OnKeyMessage, which posts the next page event: 287-293 ms from key to page change in the debug
// log on a busy unit. For the page key (message 4) the two now swap; the other keys keep the stock order (Lap:
// click, then the lap tone).
//
// Build: python3 tools/asm2patch.py patches/P14-page-key-first.c
// @cave 0xd03dd000          after the stock image (build.py grows the image, see GROW_END)
// @hook 0xd026f3b2 key_listeners  HRApp_ProcessKeyMessage: bl Ui_DispatchKeyListeners -> key_listeners
// @hook 0xd026f3b8 key_ride  HRApp_ProcessKeyMessage: bl Ride_OnKeyMessage -> key_ride
// @sym Ui_DispatchKeyListeners 0xd023cfa0
// @sym Ride_OnKeyMessage 0xd01a8fb6
#define PAGE_KEY 4

extern void Ui_DispatchKeyListeners(int msg, int arg);
extern void Ride_OnKeyMessage(int msg);

void key_listeners(int msg, int arg)
{
    if (msg == PAGE_KEY)
        Ride_OnKeyMessage(msg);
    Ui_DispatchKeyListeners(msg, arg);
}

void key_ride(int msg)
{
    if (msg != PAGE_KEY)
        Ride_OnKeyMessage(msg);
}
