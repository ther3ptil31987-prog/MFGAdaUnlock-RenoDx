// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/overlay_shortcut.hpp"
#include <iostream>
#include <stdexcept>
using namespace mfgunlock::xemfg::native;
void Require(bool value,const char* reason) { if(!value) throw std::runtime_error(reason); }
int main() { try {
  OverlayShortcut key;
  auto tap=[&] { key.Key(VK_HOME,true); key.Key(VK_HOME,false); };
  key.Key(VK_HOME,true); key.ObservedNative(); key.Key(VK_HOME,false);
  Require(key.Take()==0,"native Home toggled twice");
  tap(); Require(key.Take()==1 && key.Take()==0,"completed tap was lost or replayed");
  tap(); tap(); Require(key.Take()==2,"multiple taps collapsed into one");
  key.Key(VK_HOME,true); key.Key(VK_HOME,true,true); key.Key(VK_HOME,false);
  Require(key.Take()==1,"autorepeat created another tap");
  tap(); key.ObservedNative(); Require(key.Take()==0,"late native edge not associated with its tap");
  tap(); key.Key(VK_HOME,true); key.ObservedNative(); key.Key(VK_HOME,false);
  Require(key.Take()==1,"later native press erased an earlier lost tap");
  tap(); Require(!key.Take(true),"text/widget capture toggled the overlay");
  key.Key(VK_HOME,true); key.Take(true); key.Key(VK_HOME,false);
  Require(!key.Take(),"press consumed by text input recovered after release");
  key.Key(VK_CONTROL,true); tap(); Require(!key.Take(),"extra modifier accepted in exact mode");
  key.Key(VK_CONTROL,false);
  key.Configure({VK_F2,1,true});
  tap(); Require(!key.Take(),"hardcoded Home ignored custom shortcut");
  key.Key(VK_CONTROL,true); key.Key(VK_F2,true); key.Key(VK_F2,false);
  Require(key.Take()==1,"configured Ctrl-F2 tap lost");
  key.Configure({VK_F2,0,false}); key.Key(VK_F2,true); key.Key(VK_F2,false);
  Require(key.Take()==1,"optional modifiers not respected");
  key.Configure({VK_HOME,0,true}); key.Cancel();
  tap(); key.Configure({0,0,true}); Require(!key.Take(),"rebind replayed stale tap");
  tap(); Require(!key.Take(),"disabled shortcut recovered a tap");
  key.Configure({VK_HOME,0,true});
  for(unsigned i=0;i<20;++i) tap();
  Require(key.Take()==16 && key.Dropped()==4,"tap queue is unbounded or overflow is hidden");
  tap(); key.Cancel(); Require(!key.Take(),"cancel retained old input");
  OverlayShortcut receipt;
  receipt.Key(VK_HOME,true); receipt.ObservedNative(); receipt.Key(VK_HOME,false);
  auto seen=receipt.Drain();
  Require(seen.presses==1 && seen.releases==1 && seen.matched==1 && seen.native==1
      && seen.queued==1 && seen.handled==1 && !seen.ready && !seen.blocked,"receipt lost native deduplication reason");
  receipt.Key(VK_HOME,true); receipt.Key(VK_HOME,false); seen=receipt.Drain(true);
  Require(seen.queued==1 && seen.blocked==1 && !seen.handled && !seen.ready,"blocked receipt claims recovery");
  receipt.Key(VK_CONTROL,true); receipt.Key(VK_HOME,true); receipt.Key(VK_HOME,false); seen=receipt.Drain();
  Require(seen.presses==3 && seen.releases==3 && seen.matched==2 && !seen.queued && seen.modifiers==1,
      "modifier mismatch not distinguishable from missing input");
  receipt.Cancel(); const auto canceled=receipt.Drain().activity;
  receipt.Cancel(); Require(receipt.Drain().activity==canceled,"unfocused idle produces unbounded log activity");
  receipt.Hotkey(); receipt.Hotkey(); seen=receipt.Drain();
  Require(seen.hotkeys==2 && seen.ready==2 && !seen.blocked,"registered shortcut notifications lost");
  receipt.Hotkey(); Require(receipt.Drain(true).blocked==1,"text capture did not consume registered shortcut");
  std::cout<<"PASS: lost taps, native deduplication, modifier/rebind/disabled policies, capture and bounded queue\n";
  return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
