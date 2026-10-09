#pragma once
#include "common.h"

void ResolveQuantumApi(const Section& text, const Section& rdata);
void EnableQuantumDrive();   // after sco::ResolveAll: mounts the drive's game data, sco::game::pak::Enable
void QuantumOnCoreLog(const char* line);   // every sco-core log line (finds the end of the DataCore load)
void LogQuantum();
bool QuantumDriveReady();   // the Gladius' new drive is patched in (LogQuantum's first line)
bool QuantumBoostReady();   // quantum boost works (LogQuantum's second line)
void ProcessQuantum();
