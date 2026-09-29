#pragma once
#include <stdint.h>

void setupEyes();
void observeEyesFace(bool found, uint32_t capturedMs);
void updateEyes(float pan, float tilt);
