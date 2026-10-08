#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdlib>

struct BridgeShip { char id[121] = {}, cls[201] = {}, name[241] = {}, state[16] = {}; unsigned long long version = 0; };
namespace bridgewire {
inline bool Hex(const std::string& s, char* out, size_t cap) {
    if (s.empty() || s.size() % 2 || s.size()/2 >= cap) return false;
    auto nib = [](char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; };
    for (size_t i=0; i<s.size(); i+=2) {
        int a=nib(s[i]), b=nib(s[i+1]); if (a<0 || b<0 || a*16+b<32 || a*16+b==127) return false;
        out[i/2]=static_cast<char>(a*16+b);
    }
    out[s.size()/2]=0; return true;
}
inline bool SafeId(const char* s) {
    if (!*s) return false;
    for (; *s; ++s) if (!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-')) return false;
    return true;
}
inline bool Fleet(const std::string& input, std::vector<BridgeShip>& out) {
    out.clear(); if (input.size()>262144 || input.empty() || input.back()!='\n') return false;
    std::istringstream stream(input); std::string line; if (!std::getline(stream,line) || line!="SCBRIDGE1") return false;
    std::vector<BridgeShip> parsed;
    while (std::getline(stream,line)) {
        if (parsed.size()>=256) return false;
        std::string fields[5]; size_t start=0;
        for (int i=0;i<4;++i) { size_t end=line.find('\t',start); if(end==std::string::npos)return false; fields[i]=line.substr(start,end-start);start=end+1; }
        fields[4]=line.substr(start); BridgeShip ship;
        if(!Hex(fields[0],ship.id,sizeof(ship.id))||!Hex(fields[1],ship.cls,sizeof(ship.cls))||!Hex(fields[2],ship.name,sizeof(ship.name))||!Hex(fields[3],ship.state,sizeof(ship.state)))return false;
        if(!SafeId(ship.id)||!SafeId(ship.cls)||fields[4].empty()||fields[4].size()>16||fields[4].find_first_not_of("0123456789")!=std::string::npos)return false;
        ship.version=std::strtoull(fields[4].c_str(),nullptr,10);
        if(!ship.version||ship.version>9007199254740991ULL)return false;
        if(std::strcmp(ship.state,"stored")&&std::strcmp(ship.state,"deployed")&&std::strcmp(ship.state,"reserved"))return false;
        for(const auto& prior:parsed)if(!std::strcmp(prior.id,ship.id))return false;
        parsed.push_back(ship);
    }
    out.swap(parsed); return true;
}
}
