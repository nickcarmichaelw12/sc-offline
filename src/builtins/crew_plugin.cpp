// crew: the Crew & seats panel as a built-in plugin.
//
// Commands over the panel's seat actions in spawner.cpp, gated on the "crew" capability (seat
// control found on this game build). Each queues the same request the Crew tab's buttons queue;
// the built-in's tick subscription runs ProcessCrew, which carries it out and puts the result on
// the status strip as before. The ship the commands work on is the panel's target ship: the one
// you spawned last, or the one crew.target picked. The mechanics stay in spawner.cpp.
#include "builtins.h"
#include "tabs.h"
#include "../menu.h"
#include "../spawner.h"
#include "../teleport.h"
#include "../version.h"
#include <cstdio>

namespace {

bool g_ticking = false;   // the tick subscription owns ProcessCrew

const sco_plugin_info kInfo = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "crew", SCO_VERSION, "sc-offline",
};

constexpr const char* kCap = "crew";
constexpr int kMaxSeats = 128;

// The NPC list is npcs.txt, read once you're in the universe; asking for it starts the read.
// -1: the list isn't loaded yet; -2: no NPC by that name.
int NpcIndex(const char* name) {
    const int n = Menu_NpcCount();
    if (n < 0) return -1;
    const int i = FindBuiltinName(n, Menu_NpcName, name);
    return i < 0 ? -2 : i;
}

// The target ship's seats, as the panel lists them; -1 when there is no target ship.
int Seats(MenuSeat* seats, char* ship, size_t shipLen) { return Menu_GetSeats(seats, kMaxSeats, ship, shipLen); }

bool HasTarget() { return Menu_GetSeats(nullptr, 0, nullptr, 0) >= 0; }

sco_result NoTarget(char* reply, uint32_t size) {
    snprintf(reply, size, "No target ship: spawn one, or sit in one and run crew.target");
    return SCO_FAILED;
}

sco_result Target(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    Menu_TargetShipImIn();
    snprintf(reply, size, "Targeting the ship you're in; the status strip says which");
    return SCO_OK;
}

// Words matched against the seat's name, as the Vehicles tab's "seat by name" does (any case).
bool SeatMatches(const char* seat, const char* words) {
    char s[64], w[64];
    strncpy_s(s, seat, _TRUNCATE);
    strncpy_s(w, words, _TRUNCATE);
    _strlwr_s(s);
    _strlwr_s(w);
    char* next = nullptr;
    for (char* word = strtok_s(w, " ", &next); word; word = strtok_s(nullptr, " ", &next))
        if (!strstr(s, word)) return false;
    return true;
}

sco_result Sit(const sco_arg* args, uint32_t, void*, char* reply, uint32_t size) {
    const char* words = args[0].v.s;
    if (!words || !*words || strlen(words) > 47) {
        snprintf(reply, size, "Name the seat: words from its name, at most 47 characters (\"pilot\", \"turret left\")");
        return SCO_BAD_ARG;
    }
    static MenuSeat seats[kMaxSeats];
    char ship[64] = "";
    const int n = Seats(seats, ship, sizeof(ship));
    if (n < 0) return NoTarget(reply, size);
    for (int i = 0; i < n && i < kMaxSeats; ++i) {
        if (!SeatMatches(seats[i].name, words)) continue;
        if (seats[i].state == SeatState_You) {
            snprintf(reply, size, "You're already in %s", seats[i].name);
            return SCO_OK;
        }
        Menu_RequestSit(seats[i].id, true);
        snprintf(reply, size, "Sitting you in %s on %s", seats[i].name, ship);
        return SCO_OK;
    }
    snprintf(reply, size, "%s has no seat matching '%s'", ship[0] ? ship : "The target ship", words);
    return SCO_FAILED;
}

sco_result StandAll(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    if (!HasTarget()) return NoTarget(reply, size);
    Menu_RequestStandAll();
    snprintf(reply, size, "Everyone on the target ship stands up");
    return SCO_OK;
}

sco_result Fill(const sco_arg* args, uint32_t, void*, char* reply, uint32_t size) {
    const char* npc = args[0].v.s;
    if (!npc || !*npc) {
        snprintf(reply, size, "Name an NPC archetype from npcs.txt");
        return SCO_BAD_ARG;
    }
    if (!HasTarget()) return NoTarget(reply, size);
    const int i = NpcIndex(npc);
    if (i == -1) {
        snprintf(reply, size, "The NPC list loads once you're in the universe; try again in a moment");
        return SCO_FAILED;
    }
    if (i == -2) {
        snprintf(reply, size, "'%s' isn't in the NPC list (npcs.txt)", npc);
        return SCO_FAILED;
    }
    Menu_RequestFillCrew(i);
    snprintf(reply, size, "Filling the empty seats with %s", Menu_NpcName(i));
    return SCO_OK;
}

sco_result Clear(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    if (!HasTarget()) return NoTarget(reply, size);
    Menu_RequestClearCrew();
    snprintf(reply, size, "Removing the NPC crew from the target ship");
    return SCO_OK;
}

sco_result PowerOn(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    if (!HasTarget()) return NoTarget(reply, size);
    Menu_RequestFlightReady();
    snprintf(reply, size, "Sending Flight Ready to the target ship");
    return SCO_OK;
}

void OnTick(const char*, const void* data, void*) {
    if (data && g_tp.ok) ProcessCrew(*static_cast<const uint32_t*>(data));
}

const sco_plugin_info* CrewQuery() { return &kInfo; }

sco_result CrewLoad(const sco_api* api, sco_plugin* self) {
    const sco_arg_def seat[1] = { BuiltinArg("seat", SCO_ARG_STRING, "Words from the seat's name (pilot, copilot, turret left)") };
    const sco_arg_def npc[1] = { BuiltinArg("npc", SCO_ARG_STRING, "NPC archetype, as in npcs.txt") };
    sco_result r = RegisterBuiltinCommand(api, self, kCap, "crew.target", "Target the ship I'm in",
        "Makes the ship you're in the Crew & seats target", Target);
    if (r == SCO_OK) r = RegisterBuiltinCommand(api, self, kCap, "crew.sit", "Sit in seat",
        "Puts you in the target ship's first seat whose name has these words, removing an NPC in it", Sit, seat, 1);
    if (r == SCO_OK) r = RegisterBuiltinCommand(api, self, kCap, "crew.stand_all", "Everyone stand up",
        "You and every NPC on the target ship get out of the seats", StandAll);
    if (r == SCO_OK) r = RegisterBuiltinCommand(api, self, kCap, "crew.fill", "Fill empty seats",
        "Puts an NPC of this archetype in every empty seat of the target ship", Fill, npc, 1);
    if (r == SCO_OK) r = RegisterBuiltinCommand(api, self, kCap, "crew.clear", "Remove NPC crew",
        "Removes every NPC from the target ship's seats", Clear);
    if (r == SCO_OK) r = RegisterBuiltinCommand(api, self, kCap, "crew.power_on", "Power on",
        "Sends the game's Flight Ready event to the target ship", PowerOn);
    if (r == SCO_OK) r = api->subscribe(self, "tick", OnTick, nullptr);
    if (r != SCO_OK) return r;   // the host releases what was registered
    // Its page of the menu (and keys), through sco.ui; the menu shell draws it (tabs.h).
    RegisterBuiltinTab(api, self, "crew.crew", "Crew", kTabCrew, DrawCrewTab);
    g_ticking = true;
    return SCO_OK;
}

// A crash leaves g_ticking set, so dllmain doesn't take ProcessCrew back (as with spawn).
void CrewUnload() { g_ticking = false; }

}  // namespace

bool CrewBuiltinOwnsTick() { return g_ticking; }

const sco::plugins::Builtin kCrewBuiltin = { "crew", CrewQuery, CrewLoad, CrewUnload };
