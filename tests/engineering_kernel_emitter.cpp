#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "../src/addons/mfgunlock/thin_geometry.hpp"
int main(int argc,char** argv){
  if(argc!=4)return 2;
  namespace t=mfgunlock::thingeometry;namespace a=mfgunlock::adaptivequality;
  std::ifstream file(argv[1],std::ios::binary);std::string ptx{std::istreambuf_iterator<char>(file),{}};
  std::erase(ptx, '\r'); // Production profiles operate on normalized LF PTX.
  const std::string mode=argv[3];
  if(mode!="v3"&&mode!="v3-no-photo"&&mode!="v3-no-direction"&&mode!="v2"&&mode!="base")return 3;
  t::g_adaptive_quality_enabled=mode!="base";
  t::g_adaptive_quality_profile=mode=="v2"?a::Profile::kFlickerReducedV2:mode=="base"?a::Profile::kStableV1:a::Profile::kLuminanceDirectionalV3;
  t::g_adaptive_quality_v3_photometric=mode!="v3-no-photo";
  t::g_adaptive_quality_v3_directional_border=mode!="v3-no-direction";
  const auto layout=ptx.find("param_0[248]")!=std::string::npos?t::internal::BlendLayout::V310916:t::internal::BlendLayout::Legacy;
  std::string reason;
  if(!t::internal::RewriteValidatedWarpBlend(ptx,t::g_adaptive_quality_profile,reason,layout)){std::cerr<<reason;return 4;}
  const auto target=ptx.find(".target sm_120");if(target==std::string::npos)return 5;
  ptx.replace(target,14,".target sm_89");
  std::ofstream output(argv[2],std::ios::binary);output<<ptx;
  return output?0:6;
}
