#include "UserInterface.h"
#include <cstring>

const char* get_translation(const char* key) {
    if (strcmp(key, "USB Keyboard") == 0)  return "Teclado USB   ";
    if (strcmp(key, "USB Mouse") == 0)     return "Rat)n USB     ";
    if (strcmp(key, "USB Joystick") == 0)  return "Joystick USB  ";
    if (strcmp(key, "Mouse enabled") == 0) return "Rat)n habilitado";
    if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 habilitado";
	if (strcmp(key, "Mouse speed") == 0)   return "Velocidad Rat)n";
    return key;
}
