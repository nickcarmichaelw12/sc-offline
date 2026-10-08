#include "../src/bridge.h"
#include <cstdarg>
static char directory[MAX_PATH];
void Log(const char* format,...){va_list args;va_start(args,format);vprintf(format,args);printf("\n");va_end(args);}
bool ModLogSibling(char* path,DWORD size,const char* file){return snprintf(path,size,"%s/%s",directory,file)>0;}
int main(int argc,char** argv){
 if(argc<2||argc>3)return 1;strcpy_s(directory,argv[1]);
 const bool atcTest=argc==3;
 const bool conflict=atcTest&&!strcmp(argv[2],"conflict");
 BridgeShip ships[256];
 if(Bridge_OwnedFleet(ships,256)!=-1)return 6;
 Bridge_Refresh();char status[256];bool busy=true;int count=0;
 for(int n=0;n<100;++n){Sleep(100);count=Bridge_Snapshot(ships,256,status,sizeof(status),busy);if(!busy)break;}
 if(busy||count!=1||strcmp(ships[0].state,"stored"))return 2;
 BridgeShip owned[1];
 if(Bridge_OwnedFleet(owned,1)!=1||strcmp(owned[0].id,ships[0].id)||strcmp(owned[0].cls,"AEGS_Gladius"))return 7;
 if(Bridge_OwnedFleet(owned,0)!=0)return 8;
 if(atcTest){
  if(Bridge_RequestAtc(ships[0],0,77)||Bridge_RequestAtc(ships[0],88,0))return 10;
  if(conflict)ships[0].version+=100;
  if(!Bridge_RequestAtc(ships[0],88,77))return 11;
  if(Bridge_RequestAtc(ships[0],99,66))return 12;
  Bridge_RequestSpawn(ships[0]); // Must not overwrite the ATC reservation/route.
  Bridge_Refresh(); // Must not cancel an in-flight operation either.
  char cls[201]={};uint64_t atc=0,player=0;bool ready=false;
  for(int n=0;n<100;++n){
   Sleep(100);
   if(Bridge_TakeSpawn(cls,sizeof(cls)))return 13;
   char tiny[2]={'x',0};
   if(Bridge_TakeAtc(tiny,sizeof(tiny),atc,player)||tiny[0]!='x'||atc||player)return 14;
   if(Bridge_TakeAtc(cls,sizeof(cls),atc,player)){ready=true;break;}
   Bridge_Snapshot(ships,256,status,sizeof(status),busy);
   if(strstr(status,"Reservation unconfirmed"))break;
  }
  char pending[MAX_PATH];ModLogSibling(pending,MAX_PATH,"bridge-pending.txt");
  if(GetFileAttributesA(pending)==INVALID_FILE_ATTRIBUTES)return 15;
  if(conflict){
   if(ready||!strstr(status,"Reservation unconfirmed"))return 16;
  }else{
   if(!ready||strcmp(cls,"AEGS_Gladius")||atc!=88||player!=77)return 17;
   if(Bridge_TakeAtc(cls,sizeof(cls),atc,player)||Bridge_TakeSpawn(cls,sizeof(cls)))return 18;
   Bridge_ConfirmedEntity(0);Bridge_ConfirmedEntity(12345); // Neither can confirm ATC.
   Bridge_AtcUnconfirmed();
  }
  Bridge_RequestSpawn(ships[0]);
  if(Bridge_RequestAtc(ships[0],88,77))return 19;
  Bridge_Refresh();
  for(int n=0;n<100;++n){
   Sleep(100);Bridge_Snapshot(ships,256,status,sizeof(status),busy);
   if(strstr(status,"Unresolved spawn journal"))break;
  }
  if(!busy||!strstr(status,"Unresolved spawn journal"))return 20;
  if(Bridge_OwnedFleet(owned,1)!=1||strcmp(owned[0].state,conflict?"stored":"reserved"))return 24;
  if(Bridge_TakeAtc(cls,sizeof(cls),atc,player)||Bridge_TakeSpawn(cls,sizeof(cls)))return 21;
  if(Bridge_RequestAtc(ships[0],88,77))return 22;
  puts(conflict?"ATC reservation conflict preserved journal; no dispatch.":"ATC routing/exclusion/uncertainty tests passed; no engine call simulated.");
  return 0;
 }
 Bridge_RequestSpawn(ships[0]);char cls[201]={};bool ready=false;
 for(int n=0;n<100;++n){Sleep(100);uint64_t atc=0,player=0;if(Bridge_TakeAtc(cls,sizeof(cls),atc,player))return 23;if(Bridge_TakeSpawn(cls,sizeof(cls))){ready=true;break;}}
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
