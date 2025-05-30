#include "UserInterface.h"
#include <cstring>

const char* get_translation(const char* key) {
    if (strcmp(key, "USB Keyboard") == 0)   return "USB-Tastatur  ";
    if (strcmp(key, "USB Mouse") == 0)      return "USB-Maus      ";
    if (strcmp(key, "USB Joystick") == 0)   return "USB-Joystick  ";
    if (strcmp(key, "Mouse enabled") == 0)  return "Maus aktivert";
    if (strcmp(key, "Joy 0 enabled") == 0)  return "Joy 0 aktivert";
	if (strcmp(key, "Mouse speed") == 0)    return "Maus-geschw.";
    return key;
}
