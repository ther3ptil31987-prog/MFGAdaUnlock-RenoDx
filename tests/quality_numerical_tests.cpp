#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include "../src/addons/mfgunlock/adaptive_quality_v3.hpp"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; return EXIT_FAILURE; } } while (false)
namespace q=mfgunlock::adaptivequalityv3;
int main(int argc,char** argv){
  std::ofstream cases;
  if(argc==3 && std::string(argv[1])=="--emit"){
    std::filesystem::create_directories(argv[2]);
    std::ofstream program(std::filesystem::path(argv[2])/"photometry-program.ptx");program<<q::kRelativePhotometricError;
    cases.open(std::filesystem::path(argv[2])/"photometry-cases.csv");
    cases<<"a0,a1,a2,b0,b1,b2,c0,c1,c2,d0,d1,d2,expected_base,expected_candidate\n"<<std::setprecision(9);
  }
  uint32_t state=0x1442026;
  auto random=[&](){state=state*1664525u+1013904223u;return float(state>>8)/16777216.f;};
  size_t count=0;
  for(float exposure:{0.f,0.0001f,0.03f,0.1f,1.f,10.f,1000.f,60000.f})for(int i=0;i<2048;++i){
    q::Rgb a{exposure*random(),exposure*random(),exposure*random()},b{exposure*random(),exposure*random(),exposure*random()};
    q::Rgb c{exposure*random(),exposure*random(),exposure*random()},d{exposure*random(),exposure*random(),exposure*random()};
    const float base=q::RelativePhotometricError(a,b),candidate=q::RelativePhotometricError(c,d);
    CHECK(std::isfinite(base) && base>=0 && std::isfinite(candidate) && candidate>=0);
    CHECK(std::abs(base-q::RelativePhotometricError(b,a))<0.0001f);
    CHECK(q::RelativePhotometricError(a,a)==0);
    for(bool both:{false,true})for(float native:{0.f,.2f,.5f,.75f,1.f}){
      const float confidence=q::WarpConfidence(base,candidate,native,both);
      const float weight=q::EffectiveWarpWeight(native,.85f,confidence);
      CHECK(std::isfinite(weight) && weight>=native && weight<=1);
      CHECK(q::TaperAddedWeight(native,weight,0)==native);
      CHECK(q::ArbitrateExtraWeight(native,weight,candidate,.5f)>=native);
    }
    if(cases.is_open())cases<<a.r<<','<<a.g<<','<<a.b<<','<<b.r<<','<<b.g<<','<<b.b<<','<<c.r<<','<<c.g<<','<<c.b<<','<<d.r<<','<<d.g<<','<<d.b<<','<<base<<','<<candidate<<'\n';
    ++count;
  }
  const float nan=std::numeric_limits<float>::quiet_NaN();
  CHECK(std::isinf(q::RelativePhotometricError({nan,0,0},{0,0,0})));
  CHECK(q::EffectiveWarpWeight(.4f,.85f,0)==.4f);
  std::cout<<count<<" photometric cases; native anchors, symmetry, finite HDR range and nonfinite oracle checks passed\n";
}
