#include <cstdlib>
#include <iostream>
#include "../src/addons/mfgunlock/framecount.hpp"
#include "../src/addons/mfgunlock/loadhook.hpp"
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*,int,const char*){}
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (false)
namespace h=mfgunlock::hook;
static int failure=0,attach_calls=0,commits=0,aborts=0;
HANDLE WINAPI Snapshot(DWORD,DWORD){if(failure==1){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}return reinterpret_cast<HANDLE>(1);}
BOOL WINAPI First(HANDLE,LPTHREADENTRY32 e){
  if(failure==2){SetLastError(ERROR_BAD_LENGTH);return FALSE;}
  e->dwSize=sizeof(*e);e->th32OwnerProcessID=GetCurrentProcessId();e->th32ThreadID=GetCurrentThreadId()+100;return TRUE;
}
BOOL WINAPI Next(HANDLE,LPTHREADENTRY32){SetLastError(failure==3?ERROR_BAD_LENGTH:ERROR_NO_MORE_FILES);return FALSE;}
HANDLE WINAPI Open(DWORD,BOOL,DWORD){if(failure==4||failure==5){SetLastError(failure==4?ERROR_ACCESS_DENIED:ERROR_INVALID_PARAMETER);return nullptr;}return reinterpret_cast<HANDLE>(2);}
BOOL WINAPI Close(HANDLE){return TRUE;}
LONG WINAPI Begin(){return failure==6?ERROR_INVALID_OPERATION:NO_ERROR;}
LONG WINAPI Abort(){++aborts;return NO_ERROR;}
LONG WINAPI Commit(){++commits;return failure==9?ERROR_INVALID_OPERATION:NO_ERROR;}
LONG WINAPI Update(HANDLE){return failure==7?ERROR_ACCESS_DENIED:NO_ERROR;}
LONG WINAPI Attach(PVOID*,PVOID){++attach_calls;return failure==8?ERROR_INVALID_OPERATION:NO_ERROR;}
LONG WINAPI Detach(PVOID*,PVOID){return failure==10?ERROR_INVALID_OPERATION:NO_ERROR;}
int main(){
  h::internal::thread_operations={&Snapshot,&First,&Next,&Open,&Close};
  h::internal::detours={&Begin,&Abort,&Commit,&Update,&Attach,&Detach};
  for(int f:{1,2,3,4,6,7,8,9}){
    failure=f;attach_calls=commits=aborts=0;
    void* real=nullptr;
    std::vector<h::HookItem> hooks{{"GetTickCount",&real,reinterpret_cast<void*>(&Begin)}};
    CHECK(!h::Install(GetModuleHandleW(L"kernel32.dll"),hooks,"injected"));
    CHECK(real==nullptr);
    if(f<=7)CHECK(attach_calls==0);
  }
  for(int f:{0,5}){
    failure=f;void* real=nullptr;
    std::vector<h::HookItem> hooks{{"GetTickCount",&real,reinterpret_cast<void*>(&Begin)}};
    CHECK(h::Install(GetModuleHandleW(L"kernel32.dll"),hooks,"injected"));CHECK(real);
    auto* preserved=real;
    CHECK(!h::Install(GetModuleHandleW(L"kernel32.dll"),hooks,"already installed"));CHECK(real==preserved);
    CHECK(h::Uninstall(hooks));
  }
  failure=10;
  mfgunlock::loadhook::g_hooked=true;
  mfgunlock::loadhook::internal::g_real_load_library_w=&LoadLibraryW;
  mfgunlock::loadhook::Uninstall();
  CHECK(mfgunlock::loadhook::g_hooked && mfgunlock::loadhook::internal::g_real_load_library_w==&LoadLibraryW);
  failure=9;CHECK(!h::Uninstall({}));
  failure=1;CHECK(!h::Uninstall({}));
  failure=0;CHECK(h::Uninstall({}));
  h::internal::thread_operations={};h::internal::detours={};
  std::cout<<"thread enumeration/open/registration, attach/commit/detach failures and retained state passed\n";
}
