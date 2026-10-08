#include "../src/bridge.h"
#include <cstdarg>
static char directory[MAX_PATH];
void Log(const char* format,...){va_list args;va_start(args,format);vprintf(format,args);printf("\n");va_end(args);}
bool ModLogSibling(char* path,DWORD size,const char* file){return snprintf(path,size,"%s/%s",directory,file)>0;}
int main(int argc,char** argv){
 if(argc!=2)return 1;strcpy_s(directory,argv[1]);
 BridgeShip ships[256];
 if(Bridge_OwnedFleet(ships,256)!=-1)return 6;
 Bridge_Refresh();char status[256];bool busy=true;int count=0;
 for(int n=0;n<100;++n){Sleep(100);count=Bridge_Snapshot(ships,256,status,sizeof(status),busy);if(!busy)break;}
 if(busy||count!=1||strcmp(ships[0].state,"stored"))return 2;
 BridgeShip owned[1];
 if(Bridge_OwnedFleet(owned,1)!=1||strcmp(owned[0].id,ships[0].id)||strcmp(owned[0].cls,"AEGS_Gladius"))return 7;
 if(Bridge_OwnedFleet(owned,0)!=0)return 8;
 Bridge_RequestSpawn(ships[0]);char cls[201]={};bool ready=false;
 for(int n=0;n<100;++n){Sleep(100);if(Bridge_TakeSpawn(cls,sizeof(cls))){ready=true;break;}}
 if(!ready||strcmp(cls,"AEGS_Gladius"))return 3;
 // Simulate the game-thread acknowledgement; this is transport testing only.
 Bridge_ConfirmedEntity(12345);
 for(int n=0;n<100;++n){Sleep(100);count=Bridge_Snapshot(ships,256,status,sizeof(status),busy);if(!busy)break;}
 if(busy||count!=1||strcmp(ships[0].state,"deployed"))return 4;
 if(Bridge_OwnedFleet(owned,1)!=1||strcmp(owned[0].state,"deployed"))return 9;
 char pending[MAX_PATH];ModLogSibling(pending,MAX_PATH,"bridge-pending.txt");
 if(GetFileAttributesA(pending)!=INVALID_FILE_ATTRIBUTES)return 5;
 puts("Native bridge handshake, reserve, simulated entity confirmation and refresh passed.");return 0;
}
