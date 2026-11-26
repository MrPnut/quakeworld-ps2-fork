#ifndef PS2QW_MOD_MENU_H
#define PS2QW_MOD_MENU_H

#include "../quake/quakedef.h"

const char * PS2QW_FilterCenterPrint(const char * raw);
void PS2QW_ClearModMenu(void);
qboolean PS2QW_IsModMenuKey(int key);
qboolean PS2QW_HandleModMenuKey(int key, qboolean down);

#endif
