#include "UserInterface.h"
#include <cstring>

const char* get_translation(const char* key) {
    if (strcmp(key, "USB Keyboard") == 0)  return "Clavier USB   ";
    if (strcmp(key, "USB Mouse") == 0)     return "Souris USB    ";
    if (strcmp(key, "USB Joystick") == 0)  return "Joystick USB  ";
    if (strcmp(key, "Mouse enabled") == 0) return "Souris activ(e";
    if (strcmp(key, "Joy 0 enabled") == 0) return "Joy 0 activ(";
	if (strcmp(key, "Mouse speed") == 0)   return "Vitesse souris";
    return key;
}
