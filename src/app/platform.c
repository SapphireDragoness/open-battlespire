#include "platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

void platform_attach_console(void)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
    }
}
#else
void platform_attach_console(void)
{
}
#endif
