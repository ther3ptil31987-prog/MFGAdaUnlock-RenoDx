// SPDX-License-Identifier: MIT
#include "../src/addons/mfgunlock/xemfg/native_trace.hpp"
#include <windows.h>
#include <mutex>
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>
struct Logs {std::mutex lock;std::vector<std::string> entries;};
Logs& Store(){static auto* s=new Logs;return *s;}
namespace xess_mod {void WriteLog(const char* name,const std::string& fields) noexcept {
  std::lock_guard guard(Store().lock);Store().entries.push_back(std::string(name)+":"+fields);
  SetLastError(ERROR_ACCESS_DENIED);
}}
using namespace mfgunlock::xemfg::native;
void Require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try {
  ConfigureTrace(false,true);Require(!TraceEnabled()&&!GpuTraceEnabled(),"GPU tracing enabled without opt-in");
  {RuntimeCall call("disabled",4,22);call.Result(-1);}
  Require(Store().entries.empty(),"disabled trace emitted events");
  ConfigureTrace(true,false);StartTraceWorker();
  {RuntimeCall call("artificial_wait",4,22);Sleep(1150);call.Result(-7);}
  SetLastError(1234);{RuntimeCall call("error_preservation",4,23);call.Result(-9);}
  Require(GetLastError()==1234,"diagnostics changed the native last-error value");
  ConfigureTrace(false,false);
  bool pending{},finished{};
  {std::lock_guard guard(Store().lock);for(const auto& e:Store().entries) {
    pending|=e.find("runtime_call_pending:")!=std::string::npos && e.find("artificial_wait")!=std::string::npos;
    finished|=e.find("runtime_call:")!=std::string::npos && e.find("\"result\":-7")!=std::string::npos;
  }}
  Require(pending && finished,"in-flight stall or returned result missing");
  std::cout<<"PASS: disabled overhead path, pending-call watchdog and exact returned result\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
