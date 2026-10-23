// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
struct ID3D12Device;
struct ID3D12CommandQueue;
namespace mfgunlock::xemfg::native {
void ConfigureTrace(bool cpu,bool gpu) noexcept;
bool TraceEnabled() noexcept;
bool GpuTraceEnabled() noexcept;
void StartTraceWorker(); // outside loader; addon is pinned for the worker lifetime
class RuntimeCall {
  const char* name{};
  uint64_t context{},frame{},ticket{},start{};
  unsigned slot{~0u};
  int64_t result{};
  bool resultKnown{},failed{};
public:
  RuntimeCall(const char* operation,uint64_t output=0,uint64_t frameId=0) noexcept;
  void Result(int64_t value,bool error=false) noexcept { result=value;resultKnown=true;failed=error || value<0; }
  ~RuntimeCall();
};
// Optional source-queue timestamps and fence progress. No wait is inserted.
// This measures a queue span around Present, not a private XeFG kernel.
class QueueTrace {
  struct Impl;
  Impl* impl{};
public:
  QueueTrace(ID3D12Device*,ID3D12CommandQueue*,uint64_t output,const char* queueName="source");
  ~QueueTrace();
  void Begin(uint32_t frame);
  void End();
};
}
