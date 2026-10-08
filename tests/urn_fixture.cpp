#include "../src/offline_urn.h"
#include <cstdio>
#include <cassert>
int main() {
    unsigned char rows[3][offlineurn::Size] = {};
    offlineurn::Encode(rows[0], 0);
    offlineurn::Encode(rows[1], 1);
    assert(offlineurn::Index(rows[0],2)==0);
    assert(offlineurn::Index(rows[1],2)==1);
    assert(offlineurn::Index(rows[1],1)==-1);
    std::memcpy(rows[2], rows[0], offlineurn::Size);
    rows[2][0]=0x11;rows[2][1]=0x1e;rows[2][8]=5;rows[2][0x20]=1;
    assert(offlineurn::Index(rows[2],2)==-1);
    return std::fwrite(rows, sizeof(rows), 1, stdout)==1 ? 0 : 1;
}
