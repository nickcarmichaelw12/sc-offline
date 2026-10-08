#pragma once
#include "common.h"
#include "bridge_protocol.h"
void Bridge_Refresh();
int Bridge_OwnedFleet(BridgeShip* ships, int capacity);
int Bridge_Snapshot(BridgeShip* ships, int capacity, char* status, size_t size, bool& busy);
void Bridge_RequestSpawn(const BridgeShip& ship);
bool Bridge_TakeSpawn(char* cls, size_t size);
void Bridge_ConfirmedEntity(uint64_t entity);
void Bridge_Uncertain();
