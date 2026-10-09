// ui_probe: a test plugin for in-game checks of the menu shell and hotkeys through sco.ui (not
// shipped). A third-party plugin as any other: it finds sco.ui with query_service and draws with
// ImGui through sc-offline's frame (src/menu_imgui.h), linked against sc-offline's own ImGui.
//
//   Tab "UI probe" (order 900: after Squadron 42, before Menu). Its badge is the ping count.
//   Overlay: one line in the menu's bottom-right corner, "ui_probe: N pings".
//   Ctrl+Alt+9  bound through sco.ui to ui_probe.ping: counts, logs, sets the badge.
//   Ctrl+Alt+0  bound to ui_probe.crash: the tab's next draw faults (open the tab to see it).
//               sco-core disables ui_probe; the menu and every other tab keep working.
// At load it also tries to bind F6 (the build built-in's) and M (reserved by sc-offline) and logs
// both refusals.
// Every line goes to mod.log as "[ui_probe] ...". (Ctrl+Alt+1-8 belong to the other probes.)
#include "sco_api.h"
#include "sco_ui.h"
#include "menu_imgui.h"
#include <cstddef>
#include <cstdio>
#include <cstdarg>

namespace {

const sco_api*   g_api;
sco_plugin*      g_self;
const sco_ui_v1* g_ui;
int              g_pings;
bool             g_crashArmed;

const sco_plugin_info kInfo = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "ui_probe", "1.0.0", "sc-offline tests",
};

void Say(sco_log_level level, const char* fmt, ...) {
    char buf[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_api->log(g_self, level, buf);
}

sco_result Ping(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    ++g_pings;
    char badge[16];
    snprintf(badge, sizeof(badge), "%d", g_pings);
    g_ui->set_badge(g_self, "ui_probe.main", badge);
    snprintf(reply, size, "ui_probe ping %d", g_pings);
    Say(SCO_LOG_INFO, "ping %d", g_pings);
    return SCO_OK;
}

sco_result Crash(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    g_crashArmed = true;
    snprintf(reply, size, "ui_probe: the UI probe tab faults on its next draw");
    Say(SCO_LOG_WARN, "crash armed: open the UI probe tab");
    return SCO_OK;
}

void DrawTab(void* frame, void*) {
    sc_offline_imgui::Bind(frame);
    if (g_crashArmed) {
        g_crashArmed = false;
        Say(SCO_LOG_WARN, "faulting in ui_probe.main now");
        *static_cast<volatile int*>(nullptr) = 1;
    }
    ImGui::SeparatorText("UI probe");
    ImGui::TextWrapped("Drawn by the ui_probe plugin with ImGui %s through sc-offline's frame.", ImGui::GetVersion());
    ImGui::Text("Pings: %d", g_pings);
    if (ImGui::Button("Ping (as Ctrl+Alt+9)", ImVec2(-1, 0)))
        g_api->invoke(g_self, "ui_probe.ping", nullptr, 0, nullptr, nullptr);
    ImGui::TextWrapped("Ctrl+Alt+0, then come back to this tab: the plugin faults and sco-core disables it.");
}

void DrawOverlay(void* frame, void*) {
    sc_offline_imgui::Bind(frame);
    char line[48];
    snprintf(line, sizeof(line), "ui_probe: %d pings", g_pings);
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const ImVec2 text = ImGui::CalcTextSize(line);
    ImGui::GetForegroundDrawList()->AddText(ImVec2(size.x - text.x - 12, size.y - text.y - 8), IM_COL32(255, 220, 90, 255), line);
}

void Refused(const char* what, sco_result r) {
    char why[160] = "";
    uint32_t n = sizeof(why);
    g_ui->last_error(g_self, why, &n);
    Say(SCO_LOG_INFO, "%s -> refused (%d) \"%s\"", what, static_cast<int>(r), why);
}

sco_result Command(const char* name, const char* title, sco_command_fn fn) {
    sco_command c = {};
    c.size = sizeof(c);
    c.name = name;
    c.title = title;
    c.help = title;
    c.arg_def_size = sizeof(sco_arg_def);
    c.fn = fn;
    return g_api->register_command(g_self, &c);
}

}  // namespace

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &kInfo; }

SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    g_api = api;
    g_self = self;
    if (api->size <= offsetof(sco_api, query_service) ||
        api->query_service(SCO_UI_NAME, SCO_UI_VERSION_1_0, reinterpret_cast<const void**>(&g_ui)) != SCO_OK) {
        Say(SCO_LOG_ERROR, "sco.ui isn't published by this host");
        return SCO_UNAVAILABLE;
    }
    sco_result r = Command("ui_probe.ping", "UI probe ping", Ping);
    if (r == SCO_OK) r = Command("ui_probe.crash", "UI probe crash", Crash);
    if (r == SCO_OK) r = g_ui->register_tab(self, "ui_probe.main", "UI probe", 900, DrawTab, nullptr);
    if (r == SCO_OK) r = g_ui->register_overlay(self, "ui_probe.corner", DrawOverlay, nullptr);
    if (r == SCO_OK) r = g_ui->bind_hotkey(self, "ctrl+alt+9", "ui_probe.ping", nullptr, 0);
    if (r == SCO_OK) r = g_ui->bind_hotkey(self, "ctrl+alt+0", "ui_probe.crash", nullptr, 0);
    if (r != SCO_OK) {
        Refused("load", r);
        return r;
    }
    if ((r = g_ui->bind_hotkey(self, "f6", "ui_probe.ping", nullptr, 0)) != SCO_OK) Refused("bind f6", r);
    else Say(SCO_LOG_WARN, "bind f6 -> accepted (expected a refusal)");
    if ((r = g_ui->bind_hotkey(self, "m", "ui_probe.ping", nullptr, 0)) != SCO_OK) Refused("bind m", r);
    else Say(SCO_LOG_WARN, "bind m -> accepted (expected a refusal)");
    Say(SCO_LOG_INFO, "loaded (ImGui %s): tab ui_probe.main, overlay ui_probe.corner, Ctrl+Alt+9 = ping, Ctrl+Alt+0 = crash the tab",
        ImGui::GetVersion());
    return SCO_OK;
}

SCO_EXPORT void sco_plugin_unload(void) {}
