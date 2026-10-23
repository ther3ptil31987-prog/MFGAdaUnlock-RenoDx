// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
namespace mfgunlock::xemfg {
// Formatting callers may still allocate their fields. This queue guarantees
// bounded storage and no producer wait on disk, sink or capacity; queue-lock
// contention gets a bounded spin before a record is dropped.
class AsyncLog {
  static constexpr size_t Capacity=512,Bytes=4096;
  struct Record { size_t size{}; char text[Bytes]{}; };
  std::array<Record,Capacity> queue{};
  std::mutex queueLock,wakeLock;
  std::condition_variable wake;
  size_t read{},write{},count{};
  std::atomic<bool> stopped{},started{},writing{};
  std::atomic<uint64_t> pending{},dropped{},written{},failed{};
  std::thread worker;
  void Run(const std::function<bool(const char*,size_t)>& sink) {
    std::string batch; batch.reserve(64*Bytes);
    while(!stopped || pending) {
      batch.clear(); writing=true;
      for(size_t n=0;n<64;++n) {
        // Copy only the used bytes; the lock is held for one short memcpy.
        std::lock_guard lock(queueLock);
        if(!count) break;
        const auto& record=queue[read];
        batch.append(record.text,record.size); read=(read+1)%Capacity; --count; --pending;
      }
      if(!batch.empty()) {
        try { if(sink(batch.data(),batch.size())) ++written; else ++failed; }
        catch(...) { ++failed; }
      }
      writing=false;
      if(!pending && !stopped) {
        std::unique_lock lock(wakeLock);
        wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return pending.load()!=0 || stopped.load();});
      }
    }
  }
public:
  struct Stats { uint64_t pending{},dropped{},batches{},failed{}; bool started{}; };
  ~AsyncLog() { Stop(); }
  bool Start(std::function<bool(const char*,size_t)> sink) noexcept {
    if(started.exchange(true)) return true;
    try { worker=std::thread([this,sink=std::move(sink)]{Run(sink);}); return true; }
    catch(...) { started=false; return false; }
  }
  bool Push(uint64_t tick,uint32_t pid,const char* stage,const std::string& fields) noexcept {
    // Format outside the lock. Critical sections are single short copies, so a
    // bounded retry (microseconds, never a sleep) avoids dropping a record on
    // mere contention; a full or stopped queue still drops immediately.
    char text[Bytes];
    const int n=std::snprintf(text,Bytes,"{\"tick\":%llu,\"pid\":%u,\"event\":\"%s\"%s%s}\n",
        static_cast<unsigned long long>(tick),pid,stage,fields.empty()?"":",",fields.c_str());
    if(n<0 || size_t(n)>=Bytes) { ++dropped; return false; }
    std::unique_lock lock(queueLock,std::defer_lock);
    for(unsigned attempt=0;attempt<4096 && !lock.try_lock();++attempt) if(attempt%64==63) std::this_thread::yield();
    if(!lock.owns_lock() || count==Capacity || stopped) { ++dropped; return false; }
    auto& record=queue[write];
    std::memcpy(record.text,text,size_t(n)); record.size=size_t(n);
    write=(write+1)%Capacity; ++count; ++pending;
    lock.unlock(); wake.notify_one(); return true;
  }
  Stats Snapshot() const noexcept { return {pending,dropped,written,failed,started}; }
  bool Flush(uint32_t milliseconds) const noexcept {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    while(pending || writing) {
      if(std::chrono::steady_clock::now()>=end) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
  }
  void Stop() noexcept {
    stopped=true; wake.notify_all();
    if(worker.joinable()) worker.join();
  }
};
}
