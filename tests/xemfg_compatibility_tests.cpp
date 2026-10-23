// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/compatibility_policy.hpp"
#include <stdexcept>
#include <iostream>
using namespace mfgunlock::xemfg::native::compatibility;
void Require(bool v,const char* message) {if(!v)throw std::runtime_error(message);}
int main() {try {
  Admission a{true,true,true,false,true,true,true,true,true,true,true,true,true,false,true,true,true};
  Require(Blocker(a)==Failure::None,"complete source batch rejected");
  a.presentationSupported=false;Require(Blocker(a)==Failure::DisplayMode,"exclusive output admitted for generation");
  a.presentationSupported=true;
  a.sourceWait=false;a.requiredMarkers=false;a.rejected=true;a.firstInputFailure=Failure::SourceWait;
  Require(Blocker(a)==Failure::SourceWait,"Plague-like first failure hidden by a generic rejection");
  a.firstInputFailure=Failure::MarkerSdk;Require(Blocker(a)==Failure::MarkerSdk,"SDK rejection confused with absent markers");
  a.depth=false;Require(Blocker(a)==Failure::MissingDepth,"missing depth hidden by downstream latency");
  a.deviceLost=true;a.requested=false;Require(Blocker(a)==Failure::DeviceLost,"device fault hidden by disabled FG");
  a.deviceLost=false;a.output=false;Require(Blocker(a)==Failure::NoOutput,"absent output unclassified");
  Require(CurrentLease(7,7,false),"current session rejected");
  Require(!CurrentLease(7,8,false) && !CurrentLease(7,7,true),"input from an old output/resize admitted");
  Require(std::string(Name(Evidence::Inferred))!="confirmed","inference presented as a receipt");
  std::cout<<"PASS: independent capabilities, first latency failure, device loss and stale-session rejection\n";
  return 0;
}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
