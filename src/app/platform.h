#ifndef BS_APP_PLATFORM_H
#define BS_APP_PLATFORM_H

/* On Windows the game is a GUI program, so a double-click opens no console window. Started from a console,
 * this sends stdout and stderr (--help, errors) to it. Elsewhere it does nothing. */
void platform_attach_console(void);

#endif
