#include <cstdlib>
#include <iostream>
#include "../src/addons/mfgunlock/thin_geometry.hpp"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::abort(); } } while (false)
namespace p = mfgunlock::patching;
namespace pr = mfgunlock::pointerredirect;
static int protects = 0, fail_protect = 0, copies = 0, fail_copy = 0;
static bool block_rollback = false, blocked = false;
BOOL WINAPI Protect(LPVOID at, SIZE_T n, DWORD flags, PDWORD old) {
  if (blocked || ++protects == fail_protect) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
  return VirtualProtect(at,n,flags,old);
}
bool Copy(void* at,const void* value,size_t n) {
  if (++copies == fail_copy) { if (block_rollback) blocked = true; return false; }
  std::memcpy(at,value,n);return true;
}
void Reset() {
  protects = copies = fail_protect = fail_copy = 0;
  block_rollback = blocked = false; p::operations = {&Protect,&Copy};
}
int main() {
  CHECK(!p::Readable(reinterpret_cast<void*>(1), 8));
  CHECK(p::Bytes(reinterpret_cast<void*>(1), "bad", 3).original.empty());
  p::Write invalid{reinterpret_cast<uint8_t*>(1), {1}, {2}, true};
  std::vector<p::Write> unreadable{invalid};
  CHECK(!p::Restore(unreadable) && unreadable[0].applied);
  CHECK(!p::Apply(unreadable));
  uint64_t slots[33];
  void* allocation = VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  CHECK(allocation);
  for (size_t n : {0u,1u,32u,33u}) {
    Reset();std::fill(std::begin(slots),std::end(slots),123);
    std::vector<uint64_t*> refs;for(size_t i=0;i<n;++i)refs.push_back(&slots[i]);
    std::vector<p::Write> writes;
    CHECK(pr::Apply(refs,123,allocation,writes) == (n>=1 && n<=32));
    CHECK(p::Restore(writes));
    for(auto value:slots)CHECK(value==123);
  }
  for (int failure=1;failure<=3;++failure) {
    Reset();fail_protect=failure;
    std::fill(std::begin(slots),std::end(slots),123);
    std::vector<p::Write> writes;
    CHECK(!pr::Apply({&slots[0],&slots[1],&slots[2]},123,allocation,writes));
    CHECK(!pr::Pending(writes));
    for(auto value:slots)CHECK(value==123);
  }
  for (int failure=1;failure<=3;++failure) {
    Reset();fail_copy=failure;
    std::fill(std::begin(slots),std::end(slots),123);
    std::vector<p::Write> writes;
    CHECK(!pr::Apply({&slots[0],&slots[1],&slots[2]},123,allocation,writes));
    CHECK(!pr::Pending(writes));
    for(auto value:slots)CHECK(value==123);
  }
  Reset();std::fill(std::begin(slots),std::end(slots),123);
  std::vector<p::Write> duplicate;
  CHECK(!pr::Apply({&slots[0],&slots[0]},123,allocation,duplicate));
  CHECK(slots[0]==123);
  Reset();fail_copy=2;block_rollback=true;
  mfgunlock::thingeometry::Redirect redirect;
  redirect.allocation=allocation;
  CHECK(!pr::Apply({&slots[0],&slots[1]},123,allocation,redirect.writes));
  CHECK(pr::Pending(redirect.writes));
  CHECK(slots[0]==reinterpret_cast<uint64_t>(allocation));
  std::vector<mfgunlock::thingeometry::Redirect> redirects;
  redirects.push_back(std::move(redirect));
  mfgunlock::thingeometry::Restore(redirects);
  CHECK(redirects.size()==1 && redirects[0].allocation==allocation);
  MEMORY_BASIC_INFORMATION memory{};
  CHECK(VirtualQuery(allocation,&memory,sizeof(memory)) && memory.State==MEM_COMMIT);
  Reset();
  // Another writer changed a descriptor. Our restore must not overwrite it or free its backing allocation.
  slots[0]=456;
  mfgunlock::thingeometry::Restore(redirects);
  CHECK(slots[0]==456 && redirects.size()==1);
  slots[0]=reinterpret_cast<uint64_t>(allocation);
  mfgunlock::thingeometry::Restore(redirects);
  CHECK(redirects.empty() && slots[0]==123);
  CHECK(VirtualQuery(allocation,&memory,sizeof(memory)) && memory.State==MEM_FREE);
  p::operations={};
  std::cout << "pointer counts, preflight, copy/rollback failure, lifetime and ownership passed\n";
}
