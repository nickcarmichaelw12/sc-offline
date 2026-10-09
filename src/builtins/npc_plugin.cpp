// npc: the NPCs tab as a built-in plugin.
//
// npc.spawn and npc.clear queue the same requests as the tab's buttons, gated on the "npc"
// capability (the spawner found on this game build). The built-in's tick subscription runs
// ProcessNpcs, which reads npcs.txt once you're in the universe, spawns and removes NPCs and
// checks that removals happened. The mechanics stay in npc.cpp.
#include "builtins.h"
#include "tabs.h"
#include "../menu.h"
#include "../npc.h"
#include "../teleport.h"
#include "../version.h"
#include <cstdio>

namespace {

bool g_ticking = false;   // the tick subscription owns ProcessNpcs

const sco_plugin_info kInfo = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "npc", SCO_VERSION, "sc-offline",
};

constexpr const char* kCap = "npc";
constexpr int64_t kMaxCount = 10;   // the tab's limit; npc.cpp clamps to it too

sco_result Spawn(const sco_arg* args, uint32_t, void*, char* reply, uint32_t size) {
    const char* npc = args[0].v.s;
    const int64_t count = args[1].v.i;
    if (!npc || !*npc) {
        snprintf(reply, size, "Name an NPC archetype from npcs.txt");
        return SCO_BAD_ARG;
    }
    if (count < 1 || count > kMaxCount) {
        snprintf(reply, size, "Count must be 1 to %d", static_cast<int>(kMaxCount));
        return SCO_BAD_ARG;
    }
    const int n = Menu_NpcCount();   // asking starts the read of npcs.txt
    if (n < 0) {
        snprintf(reply, size, "The NPC list loads once you're in the universe; try again in a moment");
        return SCO_FAILED;
    }
    const int i = FindBuiltinName(n, Menu_NpcName, npc);
    if (i < 0) {
        snprintf(reply, size, "'%s' isn't in the NPC list (npcs.txt)", npc);
        return SCO_FAILED;
    }
    Menu_RequestNpc(i, static_cast<int>(count));
    snprintf(reply, size, "Spawning %d x %s in front of you", static_cast<int>(count), Menu_NpcName(i));
    return SCO_OK;
}

sco_result Clear(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    if (!CanRemoveEntities()) {
        snprintf(reply, size, "Can't remove NPCs on this game build (RemoveEntity not found)");
        return SCO_FAILED;
    }
    Menu_RequestClearNpcs();
    snprintf(reply, size, "Removing the NPCs you spawned");
    return SCO_OK;
}

void OnTick(const char*, const void*, void*) {
    if (g_tp.ok) ProcessNpcs();
}

const sco_plugin_info* NpcQuery() { return &kInfo; }

sco_result NpcLoad(const sco_api* api, sco_plugin* self) {
    const sco_arg_def spawn[2] = {
        BuiltinArg("npc", SCO_ARG_STRING, "NPC archetype, as in npcs.txt"),
        BuiltinArg("count", SCO_ARG_INT, "How many (1 to 10)"),
    };
    sco_result r = RegisterBuiltinCommand(api, self, kCap, "npc.spawn", "Spawn NPCs",
        "Spawns NPCs of an archetype in front of you, as the NPCs tab does", Spawn, spawn, 2);
    if (r == SCO_OK) r = RegisterBuiltinCommand(api, self, kCap, "npc.clear", "Clear NPCs",
        "Removes every NPC you spawned (or moves it far out of range if the game won't delete it)", Clear);
    if (r == SCO_OK) r = api->subscribe(self, "tick", OnTick, nullptr);
    if (r != SCO_OK) return r;   // the host releases what was registered
    // Its page of the menu (and keys), through sco.ui; the menu shell draws it (tabs.h).
    RegisterBuiltinTab(api, self, "npc.npcs", "NPCs", kTabNpcs, DrawNpcsTab);
    g_ticking = true;
    return SCO_OK;
}

// A crash leaves g_ticking set, so dllmain doesn't take ProcessNpcs back.
void NpcUnload() { g_ticking = false; }

}  // namespace

bool NpcBuiltinOwnsTick() { return g_ticking; }

const sco::plugins::Builtin kNpcBuiltin = { "npc", NpcQuery, NpcLoad, NpcUnload };
