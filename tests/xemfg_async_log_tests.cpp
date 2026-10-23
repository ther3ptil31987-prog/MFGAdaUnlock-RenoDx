// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/async_log.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
using mfgunlock::xemfg::AsyncLog;
namespace { void Check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); } }
int main() {
  try {
    auto storage=std::make_unique<AsyncLog>(); auto& log=*storage;
    std::promise<void> entered,release;
    auto ready=release.get_future().share();
    std::string received;
    Check(log.Push(1,10,"first","\"value\":1"),"early record not buffered");
    Check(log.Start([&](const char* text,size_t size) {
      static bool first=true;
      if(first) { first=false; entered.set_value(); ready.wait(); }
      received.append(text,size); return true;
    }),"worker start");
    const bool running=entered.get_future().wait_for(std::chrono::seconds(1))==std::future_status::ready;
    if(!running) { release.set_value(); throw std::runtime_error("worker did not enter sink"); }
    auto producer=std::async(std::launch::async,[&] { for(unsigned i=0;i<800;++i) log.Push(i+2,10,"sample",""); });
    const bool nonblocking=producer.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    const bool bounded=log.Snapshot().pending<=512 && log.Snapshot().dropped>0;
    const bool timedOut=!log.Flush(5);
    release.set_value(); producer.get();
    Check(nonblocking,"producer waited for the blocked sink");
    Check(bounded,"capacity did not bound memory or count dropped records");
    Check(timedOut,"flush claimed sink completion while blocked");
    Check(log.Flush(1000),"drain did not finish"); log.Stop();
    Check(received.find("\"event\":\"first\"")!=std::string::npos && received.back()=='\n',"incomplete JSON record");
    auto failureStorage=std::make_unique<AsyncLog>(); auto& failure=*failureStorage;
    failure.Push(1,1,"failure","");
    Check(!failure.Push(2,1,"too_large",std::string(5000,'x')),"oversized record truncated into invalid JSON");
    failure.Start([](const char*,size_t){return false;}); Check(failure.Flush(1000),"failed sink did not drain");
    Check(failure.Snapshot().failed && failure.Snapshot().dropped,"sink failure/oversize not visible");
    // Lock contention alone (producers and the draining worker) must not
    // lose records while the queue has room; only a full queue may drop.
    for(unsigned round=0;round<20;++round) {
      auto contendedStorage=std::make_unique<AsyncLog>(); auto& contended=*contendedStorage;
      std::atomic<size_t> lines{};
      contended.Start([&](const char* text,size_t size) { for(size_t i=0;i<size;++i) lines+=text[i]=='\n'; return true; });
      std::vector<std::thread> producers;
      for(unsigned p=0;p<4;++p) producers.emplace_back([&,p] { for(unsigned i=0;i<100;++i) contended.Push(i,p,"contended","\"n\":1"); });
      for(auto& t:producers) t.join();
      Check(contended.Flush(2000),"contended drain did not finish");
      Check(contended.Snapshot().dropped==0 && lines==400,"lock contention dropped records below capacity");
      contended.Stop();
    }
    std::cout<<"PASS: blocked sink does not block producers, fixed capacity, overflow, complete records, flush, sink error reporting and contention without loss\n"; return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
