#include "bridge.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <share.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

static SRWLOCK g_bridgeLock = SRWLOCK_INIT;
static BridgeShip g_fleet[256], g_selected;
static bool g_fleetReady = false;
static int g_count=0, g_phase=0; // 0 idle, 1 HTTP, 2 queued spawn, 3 awaiting entity, 4 locked/uncertain
static char g_status[256]="Not connected. Start the server, pair it, then Refresh.";
static char g_operation[33]={};
static uint64_t g_entity=0;
static bool g_reserve=false;

static void Status(const char* text, int phase) {
    AcquireSRWLockExclusive(&g_bridgeLock); strcpy_s(g_status,text);g_phase=phase;ReleaseSRWLockExclusive(&g_bridgeLock);
    Log("[bridge] %s",text);
}
static bool Journal() {
    char path[MAX_PATH], tmp[MAX_PATH]; if(!ModLogSibling(path,MAX_PATH,"bridge-pending.txt"))return false;
    if(sprintf_s(tmp,"%s.tmp",path)<0)return false;
    HANDLE f=CreateFileA(tmp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    char body[160];int n=sprintf_s(body,"%s\n%llu\n",g_operation,static_cast<unsigned long long>(g_entity));DWORD wrote=0;
    bool ok=n>0 && WriteFile(f,body,n,&wrote,nullptr) && wrote==static_cast<DWORD>(n) && FlushFileBuffers(f);CloseHandle(f);
    return ok && MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
static bool HasJournal() { char p[MAX_PATH];return ModLogSibling(p,MAX_PATH,"bridge-pending.txt")&&GetFileAttributesA(p)!=INVALID_FILE_ATTRIBUTES; }
static bool ClearJournal() { char p[MAX_PATH];return ModLogSibling(p,MAX_PATH,"bridge-pending.txt")&&DeleteFileA(p); }

struct HttpHandle { HINTERNET h; ~HttpHandle(){if(h)WinHttpCloseHandle(h);} };
static bool Http(const wchar_t* route, const std::string* body, const char* key, std::string& output) {
    char path[MAX_PATH];if(!ModLogSibling(path,MAX_PATH,"bridge.ini"))return false;
    FILE* f=_fsopen(path,"r",_SH_DENYNO);if(!f)return false;
    char portText[16]={}, token[80]={};bool read=fgets(portText,sizeof(portText),f)&&fgets(token,sizeof(token),f);fclose(f);
    if(!read)return false;token[strcspn(token,"\r\n")]=0;portText[strcspn(portText,"\r\n")]=0;
    if(strlen(token)!=64||strspn(token,"0123456789abcdef")!=64||!portText[0]||strspn(portText,"0123456789")!=strlen(portText))return false;
    unsigned long port=strtoul(portText,nullptr,10);if(!port||port>65535)return false;
    HttpHandle session{WinHttpOpen(L"SC-Offline-Bridge/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.h)return false;WinHttpSetTimeouts(session.h,1500,1500,2000,2000);
    HttpHandle connection{WinHttpConnect(session.h,L"127.0.0.1",static_cast<INTERNET_PORT>(port),0)};if(!connection.h)return false;
    HttpHandle request{WinHttpOpenRequest(connection.h,body?L"POST":L"GET",route,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0)};if(!request.h)return false;
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    std::wstring headers=L"Authorization: Bearer ";for(const char* p=token;*p;++p)headers+=static_cast<wchar_t>(*p);
    headers+=L"\r\nContent-Type: application/json\r\n";
    if(key){headers+=L"Idempotency-Key: ";for(const char* p=key;*p;++p)headers+=static_cast<wchar_t>(*p);headers+=L"\r\n";}
    if(!WinHttpSendRequest(request.h,headers.c_str(),static_cast<DWORD>(headers.size()),body?const_cast<char*>(body->data()):nullptr,body?static_cast<DWORD>(body->size()):0,body?static_cast<DWORD>(body->size()):0,0)||!WinHttpReceiveResponse(request.h,nullptr))return false;
    DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)||status!=200)return false;
    output.clear();char buf[4096];DWORD got=0;
    do {if(!WinHttpReadData(request.h,buf,sizeof(buf),&got)||output.size()+got>262144)return false;output.append(buf,got);}while(got);
    return true;
}
static DWORD WINAPI Worker(void*) {
    // Only one worker exists; phase locks prevent requests changing these inputs.
    if(g_reserve) {
        g_entity=0;
        if(!Journal()){Status("Cannot save spawn journal; no spawn attempted.",4);return 0;}
        char body[512],key[64];sprintf_s(body,"{\"ship_id\":\"%s\",\"expected_version\":%llu,\"operation_id\":\"%s\"}",g_selected.id,g_selected.version,g_operation);sprintf_s(key,"%s-reserve",g_operation);
        std::string input(body),response;
        if(!Http(L"/api/v1/bridge/reserve",&input,key,response)||response.find(g_operation)==std::string::npos||response.find("\"reserved\"")==std::string::npos){Status("Reservation unconfirmed. No spawn attempted; keep bridge-pending.txt for recovery.",4);return 0;}
        Status("Reserved. Waiting for the game to spawn your ship...",2);return 0;
    }
    if(g_entity) {
        if(!Journal()){Status("Ship exists but confirmation journal failed. Do not spawn again.",4);return 0;}
        char body[160],key[64];sprintf_s(body,"{\"operation_id\":\"%s\",\"entity_id\":\"%llu\"}",g_operation,static_cast<unsigned long long>(g_entity));sprintf_s(key,"%s-confirm",g_operation);
        std::string input(body),response;
        if(!Http(L"/api/v1/bridge/confirm",&input,key,response)||response.find("\"confirmed\"")==std::string::npos){Status("Ship exists; server confirmation uncertain. Keep journal; no automatic respawn.",4);return 0;}
        if(!ClearJournal()){Status("Server confirmed; local journal needs recovery before another spawn.",4);return 0;}
        g_entity=0;
    }
    std::string reply;std::vector<BridgeShip> fleet;
    if(!Http(L"/api/v1/bridge/fleet",nullptr,nullptr,reply)||!bridgewire::Fleet(reply,fleet)){
        AcquireSRWLockExclusive(&g_bridgeLock);g_count=0;g_fleetReady=false;ReleaseSRWLockExclusive(&g_bridgeLock);
        Status("Connection failed: check server, pairing, and mod.log. Keep offline firewall enabled.",0);return 0;
    }
    AcquireSRWLockExclusive(&g_bridgeLock);g_count=static_cast<int>(fleet.size());for(int i=0;i<g_count;++i)g_fleet[i]=fleet[i];g_fleetReady=true;ReleaseSRWLockExclusive(&g_bridgeLock);
    Status(HasJournal()?"Unresolved spawn journal. Fleet readable; spawning locked until reconciled.":"Connected to local server. Owned fleet loaded.",HasJournal()?4:0);return 0;
}
static void Launch() {HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);else Status("Cannot start bridge worker.",4);}
void Bridge_Refresh() {
    AcquireSRWLockExclusive(&g_bridgeLock);if(g_phase==1||g_phase==2||g_phase==3){ReleaseSRWLockExclusive(&g_bridgeLock);return;}g_phase=1;g_reserve=false;g_entity=0;ReleaseSRWLockExclusive(&g_bridgeLock);Launch();
}
int Bridge_OwnedFleet(BridgeShip* ships, int capacity) {
    AcquireSRWLockShared(&g_bridgeLock);
    const int n = g_fleetReady ? (g_count < capacity ? g_count : capacity) : -1;
    for (int i = 0; i < n; ++i) ships[i] = g_fleet[i];
    ReleaseSRWLockShared(&g_bridgeLock);
    return n;
}
int Bridge_Snapshot(BridgeShip* ships,int capacity,char* status,size_t size,bool& busy){AcquireSRWLockShared(&g_bridgeLock);int n=g_count<capacity?g_count:capacity;for(int i=0;i<n;++i)ships[i]=g_fleet[i];strncpy_s(status,size,g_status,_TRUNCATE);busy=g_phase!=0;ReleaseSRWLockShared(&g_bridgeLock);return n;}
void Bridge_RequestSpawn(const BridgeShip& ship) {
    AcquireSRWLockExclusive(&g_bridgeLock);
    if(g_phase!=0||strcmp(ship.state,"stored")||HasJournal()){ReleaseSRWLockExclusive(&g_bridgeLock);return;}
    unsigned char random[16];if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0){ReleaseSRWLockExclusive(&g_bridgeLock);return;}
    for(int i=0;i<16;++i)sprintf_s(g_operation+2*i,3,"%02x",random[i]);
    g_selected=ship;g_reserve=true;g_entity=0;g_phase=1;ReleaseSRWLockExclusive(&g_bridgeLock);Launch();
}
bool Bridge_TakeSpawn(char* cls,size_t size){AcquireSRWLockExclusive(&g_bridgeLock);bool ok=g_phase==2;if(ok){strncpy_s(cls,size,g_selected.cls,_TRUNCATE);g_phase=3;}ReleaseSRWLockExclusive(&g_bridgeLock);return ok;}
void Bridge_ConfirmedEntity(uint64_t entity){AcquireSRWLockExclusive(&g_bridgeLock);if(g_phase!=3){ReleaseSRWLockExclusive(&g_bridgeLock);return;}g_entity=entity;g_reserve=false;g_phase=1;ReleaseSRWLockExclusive(&g_bridgeLock);Launch();}
void Bridge_Uncertain(){Status("Spawn outcome uncertain. Reservation kept; no retry. Keep bridge-pending.txt for recovery.",4);}
