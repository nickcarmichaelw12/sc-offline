#include "../src/bridge_protocol.h"
#include <cassert>
int main(){
 std::vector<BridgeShip> ships;
 const std::string valid="SCBRIDGE1\n616263\t414547535f476c6164697573\t476c6164697573\t73746f726564\t1\n";
 assert(bridgewire::Fleet(valid,ships)&&ships.size()==1&&ships[0].version==1);
 assert(!bridgewire::Fleet(valid.substr(0,valid.size()-1),ships)&&ships.empty());
 assert(!bridgewire::Fleet("SCBRIDGE2\n",ships));
 assert(!bridgewire::Fleet("SCBRIDGE1\n00\t61\t61\t73746f726564\t1\n",ships));
 assert(!bridgewire::Fleet(valid+valid.substr(10),ships));
 assert(bridgewire::Fleet("SCBRIDGE1\n",ships)&&ships.empty());
 char out[3];assert(!bridgewire::Hex("610a",out,3));assert(!bridgewire::Hex("616263",out,3));
 assert(!bridgewire::SafeId("unsafe\"id"));assert(bridgewire::SafeId("AEGS_Gladius"));
}
