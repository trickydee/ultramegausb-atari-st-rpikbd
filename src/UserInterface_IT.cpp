#include "UserInterface.h"
#include <cstring>

const char* get_translation(const char* key) {
    if (strcmp(key, "USB Keyboard") == 0)  return "Tastiera USB  ";
    if (strcmp(key, "USB Mouse") == 0)     return "Mouse USB     ";
    if (strcmp(key, "USB Joystick") == 0)  return "Joystick USB  ";
    if (strcmp(key, "Mouse enabled") == 0) return "Mouse abilitato";
    if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 abilitato";
	if (strcmp(key, "Mouse speed") == 0)   return "Velocit+ mouse";
    return key;
}
