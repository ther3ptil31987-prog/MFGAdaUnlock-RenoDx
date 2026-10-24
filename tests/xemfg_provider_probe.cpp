// Offline process-local patch/restore exercise. No feature is created and no
// GPU work is submitted. Vendor DLL initialization builds its metadata tables.
#include "../src/addons/mfgunlock/xemfg/vendor/port/port_support.h"
#include "../src/addons/mfgunlock/xemfg/vendor/port/shader_port.h"
#include "../src/addons/mfgunlock/xemfg/vendor/port/model_port.h"
#include "../src/addons/mfgunlock/xemfg/vendor/host_unlock.h"
#include "../src/addons/mfgunlock/backend_selection.hpp"
#include <iostream>
#include "../src/addons/mfgunlock/xemfg/native_reflex_profile.hpp"

namespace xess_mod {
HMODULE Self() noexcept { return GetModuleHandleW(nullptr); }
void WriteLog(const char* stage,const std::string& fields) noexcept { std::cout<<stage<<": "<<fields<<'\n'; }
}
std::vector<unsigned char> Text(HMODULE m) {
  auto* b=reinterpret_cast<unsigned char*>(m);
  auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(b+reinterpret_cast<IMAGE_DOS_HEADER*>(b)->e_lfanew);
  std::vector<unsigned char> out;
  auto* sections=IMAGE_FIRST_SECTION(nt);
  for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
    if (sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
      out.insert(out.end(),b+sections[i].VirtualAddress,b+sections[i].VirtualAddress+sections[i].Misc.VirtualSize);
  return out;
}
int wmain(int argc,wchar_t** argv) {
  if (argc!=3) return 77;
  try {
    if (mfgunlock::backend::Parse(L"DLSSG")!=mfgunlock::backend::Kind::DlssG
        || mfgunlock::backend::Parse(L"XeMFG")!=mfgunlock::backend::Kind::XeMfg
        || mfgunlock::backend::Parse(L"garbage")!=mfgunlock::backend::Kind::Invalid
        // 1.5.2: case and padding are tolerated; an invalid value keeps DLSS-G.
        || mfgunlock::backend::Parse(L" dlssg ")!=mfgunlock::backend::Kind::DlssG
        || mfgunlock::backend::Parse(L"XEMFG")!=mfgunlock::backend::Kind::XeMfg
        || mfgunlock::backend::Effective(mfgunlock::backend::Parse(L"DLSS-G"))!=mfgunlock::backend::Kind::DlssG
        || mfgunlock::backend::Effective(mfgunlock::backend::Kind::XeMfg)!=mfgunlock::backend::Kind::XeMfg)
      throw std::runtime_error("backend_policy");
    auto fg=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto ll=LoadLibraryExW(argv[2],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!fg || !ll || !xess_layout::Pair(fg,ll)) throw std::runtime_error("layout_pair");
    if(!mfgunlock::xemfg::native::reflex_profile::Available(fg)) throw std::runtime_error("external_latency_profile");
    std::array<unsigned char,342> setter{};
    if(!xess_layout::Image(fg).Read(0x20e620,setter.data(),setter.size())) throw std::runtime_error("external_latency_read");
    for(size_t i=0;i<setter.size();++i) {
      setter[i]^=1;
      if(mfgunlock::xemfg::native::reflex_profile::SetterMatches(setter)) throw std::runtime_error("mutated_external_latency_accepted");
      setter[i]^=1;
    }
    const auto a=Text(fg),b=Text(ll);
    // Incompatible capacity is rejected without any change.
    for (unsigned n: {0,1,6,255}) {
      bool rejected=false;
      try { xess_host_unlock::Unlock invalid(fg,ll,uint8_t(n),false); }
      catch (const std::exception&) { rejected=true; }
      if (!rejected || Text(fg)!=a || Text(ll)!=b) throw std::runtime_error("bad_count_changed_image");
    }
    // Mutation with the same image dimensions must be rejected before writing.
    const auto rva=xess_layout::Rva(fg,0x20973b);
    auto* site=reinterpret_cast<unsigned char*>(fg)+rva;
    DWORD p{},unused{}; if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&p)) throw std::runtime_error("mutation_protect");
    site[1]^=1; VirtualProtect(site,5,p,&unused); FlushInstructionCache(GetCurrentProcess(),site,5);
    bool rejected=false;
    try { xess_host_unlock::Unlock invalid(fg,ll,5,false); }
    catch (const std::exception&) { rejected=true; }
    VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&p); site[1]^=1;
    VirtualProtect(site,5,p,&unused); FlushInstructionCache(GetCurrentProcess(),site,5);
    if (!rejected || Text(fg)!=a || Text(ll)!=b) throw std::runtime_error("mutated_site_accepted");
    for (unsigned n: {2,3,5}) {
      ShaderPort shaders(fg,GetModuleHandleW(nullptr));
      ModelPort model(fg,true,true);
      xess_host_unlock::Unlock unlock(fg,ll,uint8_t(n),false);
      if (unlock.Keeps2xModel() || unlock.Applied().size()<4) throw std::runtime_error("unlock_receipt");
      unlock.Restore(); model.restore();
    }
    if (Text(fg)!=a || Text(ll)!=b) throw std::runtime_error("text_restore_mismatch");
    std::cout<<"PASS: payload identities; 4 invalid counts; mutated site refusal; 3 complete model/unlock/restore cycles\n";
    // The library's original static metadata must also survive normal unload.
    FreeLibrary(ll); FreeLibrary(fg);
    return 0;
  } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
