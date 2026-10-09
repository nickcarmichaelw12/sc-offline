// The npc built-in's menu tab: NPCs, and the NPC picker the Crew tab shares. Moved from menu.cpp
// unchanged; registered through sco.ui by npc_plugin.cpp and drawn by the menu shell.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

// --- NPC picker, shared by the NPCs and Crew tabs -------------------------------------------

int g_npcPick = 0;

bool NpcPicker() {
    static char filter[64] = "";
    const int npcs = Menu_NpcCount();
    if (npcs < 0) { Hint("Loading NPCs (you need to be in the universe)..."); return false; }
    if (npcs == 0) { Hint("No NPCs found. Check data\\npcs.txt."); return false; }
    if (g_npcPick >= npcs) g_npcPick = 0;
    if (SearchBox("##npcFilter", "Search NPCs", filter, sizeof(filter)))
        for (int i = 0; i < npcs; ++i)
            if (MatchesFilter(Menu_NpcName(i), filter)) { g_npcPick = i; break; }
    char preview[128];
    PrettyBuildName(preview, sizeof(preview), Menu_NpcName(g_npcPick));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##npc", preview, ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < npcs; ++i) {
            const char* name = Menu_NpcName(i);
            if (!MatchesFilter(name, filter)) continue;
            char label[160];
            PrettyBuildName(label, sizeof(label) - 16, name);
            snprintf(label + strlen(label), 16, "##n%d", i);
            if (ImGui::Selectable(label, i == g_npcPick)) g_npcPick = i;
            ImGui::SetItemTooltip("%s", name);
            if (i == g_npcPick) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return true;
}

void DrawNpcsTab(void*, void*) {
    SectionHeading("Spawn NPCs");
    if (!NpcPicker()) return;
    static int howMany = 1;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##howMany", &howMany, 1, 10, howMany == 1 ? "1 NPC" : "%d NPCs");
    if (PrimaryButton("Spawn in front of me")) { Menu_RequestNpc(g_npcPick, howMany); MenuClose(); }
    if (ImGui::Button("Remove spawned NPCs", ImVec2(-1, 0))) Menu_RequestClearNpcs();
    Hint("Removes every NPC this menu has spawned, crew included.");
}
