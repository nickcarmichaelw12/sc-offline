#pragma once
// A built-in's own sco.storage database (sco-core's docs/storage.md): data/storage/<id>.db.
//
// The built-in opens it in its load, through the ordinary sco_api query_service, and closes it in
// its unload; its feature's file (teleport.cpp, travel.cpp, contracts.cpp) reads and writes
// through it on the game thread. Each of those keeps its old txt file as the fallback: with no
// storage (the built-in didn't load, the host has none, or a call fails) it reads and writes the
// txt file as before, and says so in mod.log.
//
// Import rule, the same for every file: storage records the file's last-write time when it
// imports the file (and, for wallet.txt, every time the mod writes it). A file whose time differs
// from the recorded one, or that storage has never seen, was changed outside storage since (by
// hand, by an older sc-offline, or by the txt fallback) and is imported again; otherwise storage
// holds the newer state and the file is left alone.
#include "../common.h"
#include "scosdk/storage.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

class BuiltinStore {
public:
    explicit constexpr BuiltinStore(const char* id) : id_(id) {}

    const char* Id() const { return id_; }
    explicit operator bool() const { return static_cast<bool>(s_); }
    sco::sdk::Storage& S() { return s_; }

    // From the built-in's load. Without storage its feature keeps its txt file.
    void Open(const sco_api* api, sco_plugin* self) {
        const sco_result r = s_.Open(api, self);
        if (r != SCO_OK) Log("[storage] %s: no sco.storage (result %d); it keeps its txt file", id_, static_cast<int>(r));
    }
    void Close() { s_ = {}; }

    // A failed call: logged with storage's own message, then the caller falls back to the txt file.
    void Failed(const char* what, sco_result r, const char* fallback) {
        const std::string why = s_.LastError();
        Log("[storage] %s: %s failed (result %d%s%s); %s", id_, what, static_cast<int>(r), why.empty() ? "" : ": ",
            why.c_str(), fallback);
    }

    // Integers are stored as decimal text, so the database reads plainly in any SQLite browser.
    sco_result GetInt(const char* key, int64_t& out) {
        std::string text;
        const sco_result r = s_.Get(key, text);
        if (r != SCO_OK) return r;
        char* end = nullptr;
        const long long v = strtoll(text.c_str(), &end, 10);
        if (text.empty() || *end) return SCO_BAD_ARG;
        out = v;
        return SCO_OK;
    }
    sco_result PutInt(const char* key, int64_t v) {
        char text[24];
        snprintf(text, sizeof(text), "%lld", static_cast<long long>(v));
        return s_.Put(key, text);
    }

private:
    const char*       id_;
    sco::sdk::Storage s_;
};

// The file's last-write time (FILETIME as 100 ns ticks), or 0 when it doesn't exist.
inline int64_t FileWriteTime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return 0;
    return static_cast<int64_t>((static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime);
}

// The built-ins' databases: data/storage/teleport.db, quantum.db, contracts.db. Open while that
// built-in is loaded.
inline BuiltinStore g_teleportStore{ "teleport" };    // the F7 / F8 spot (spawn.txt)
inline BuiltinStore g_quantumStore{ "quantum" };      // the Travel tab's saved spots (bookmarks.txt)
inline BuiltinStore g_contractsStore{ "contracts" };  // the wallet (wallet.txt)
