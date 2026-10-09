#!/usr/bin/env python3
"""Compile actual owned-ASOP queue/dispatch bodies with isolated dependencies.

Native ABI/SEH remains covered by the Windows DLL build. This fixture tests
identity selection and dispatch control flow, not native placement or transport.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/hooks.cpp').read_text()

def body(name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end].replace('__try', 'try').replace(
        '__except (EXCEPTION_EXECUTE_HANDLER)', 'catch (...)')

fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
using LONG = long;
LONG InterlockedIncrement(volatile LONG* p) { LONG n=*p+1; *p=n; return n; }
template<class... A> void Log(const char*, A...) {}
struct BridgeShip { char id[16], cls[32], state[16]; };
struct Ship { char serverId[16], name[32]; };
Ship g_ships[] = {{"owned", "AEGS_Gladius"}};
constexpr uintptr_t kAsopAtcId=0;
void Available() {}
auto g_getATCComp=&Available, g_requestTakingOff=&Available;
uint64_t raw=0x100000000020ULL, entity=0xfedcba9876543210ULL;
uint64_t terminal=123, queued=0, passed=0;
bool take=true;
int requests=0, dispatches=0, uncertain=0;
template<class T> T Rd(uintptr_t) { return static_cast<T>(terminal); }
uint64_t LocalPlayerId() { return raw; }
uint64_t LocalPlayerEntityId() { return entity; }
int Bridge_OwnedFleet(BridgeShip* out, int) {
    *out=BridgeShip{"owned", "AEGS_Gladius", "stored"}; return 1;
}
bool Bridge_RequestAtc(const BridgeShip&, uint64_t, uint64_t player) {
    ++requests; queued=player; return true;
}
void Bridge_DiagnosticOperation(char* out, size_t) { std::strcpy(out,"operation"); }
bool Bridge_TakeAtc(char* cls, size_t, uint64_t& atc, uint64_t& player) {
    if(!take) return false;
    std::strcpy(cls,"AEGS_Gladius"); atc=terminal; player=queued; return true;
}
const char* RequestShipFromAtc(uint64_t, uint64_t player, const char*) {
    ++dispatches; passed=player; return nullptr;
}
void Bridge_AtcUnconfirmed() { ++uncertain; }
'''
fixture += '\n' + body('QueueOwnedAtc') + '\n' + body('ProcessAtcDelivery')
fixture += r'''
int main() {
    const uint64_t spawned=entity;
    QueueOwnedAtc(1,0); assert(requests==1 && queued==spawned && queued!=raw);
    ProcessAtcDelivery(); assert(dispatches==1 && passed==spawned && uncertain==1);
    entity=spawned+1;
    ProcessAtcDelivery(); assert(dispatches==1 && uncertain==2);
    entity=0;
    ProcessAtcDelivery(); assert(dispatches==1 && uncertain==3);
    QueueOwnedAtc(1,0); assert(requests==1); // No fallback to nonzero raw value.
    entity=spawned; queued=0;
    ProcessAtcDelivery(); assert(dispatches==1 && uncertain==4);
    take=false;
    ProcessAtcDelivery(); assert(dispatches==1 && uncertain==4);
}
'''
with tempfile.TemporaryDirectory() as folder:
    cpp = Path(folder) / 'identity.cpp'
    exe = Path(folder) / 'identity'
    cpp.write_text(fixture)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: owned-ASOP entity selection; unchanged dispatch; changed/unavailable/zero rejection; uncertainty preserved')
