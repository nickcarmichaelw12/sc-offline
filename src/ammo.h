#pragma once
#include "common.h"

void ResolveAmmoApi(const Section& text);
void ProcessAmmo();
bool AmmoReady();   // the magazine setter is hooked (ResolveAmmoApi's "[+] infinite ammo: ready")
