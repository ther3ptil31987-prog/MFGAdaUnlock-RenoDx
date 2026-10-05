/* SPDX-License-Identifier: MIT
 * Opt-in bounded long-test telemetry. No per-frame I/O or blocking writes.
 */
#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <sstream>
#include <vector>
namespace mfgunlock::latencytrace {
struct Row {
  uint64_t ms, epoch, frame;
  uint32_t units, fresh, source_fps, multiplier, queue_us, pipeline_us, gpu_us, ai_us;
  uint32_t proposed_cap, accepted_limit_us, vsync, dynamic, mode;
  uint64_t previous_frame, previous_gpu_end, gpu_end, simulation_start, present_start, qpc_frequency;
  uint32_t raw_gpu_frame_us;
};
inline std::atomic_bool recording{false};
inline std::atomic<uint32_t> dropped{0};
inline SRWLOCK lock = SRWLOCK_INIT;
inline std::array<Row, 3600> rows{};
inline size_t written = 0;
inline void Clear() {
  AcquireSRWLockExclusive(&lock); written = 0; dropped.store(0);
  ReleaseSRWLockExclusive(&lock);
}
inline void Record(const Row& row) {
  if (!recording.load(std::memory_order_relaxed)) return;
  if (!TryAcquireSRWLockExclusive(&lock)) { dropped.fetch_add(1); return; }
  rows[written++ % rows.size()] = row;
  ReleaseSRWLockExclusive(&lock);
}
inline std::string Csv() {
  // Called only on explicit export from the UI, never from Present.
  std::vector<Row> copy; copy.reserve(rows.size());
  AcquireSRWLockShared(&lock);
  const size_t first = written > rows.size() ? written - rows.size() : 0;
  for (size_t i = first; i < written; ++i) copy.push_back(rows[i % rows.size()]);
  const auto dropped_count = dropped.load();
  ReleaseSRWLockShared(&lock);
  std::ostringstream s;
  s << "# MFG latency trace v1; dropped_samples=" << dropped_count << "\n"
       "uptime_ms,epoch,frame,units,fresh,source_fps,multiplier,queue_us,pipeline_us,gpu_us,ai_us,proposed_cap,accepted_limit_us,driver_vsync,dynamic,guard_mode,previous_frame,previous_gpu_end,gpu_end,simulation_start,present_start,qpc_frequency,raw_gpu_frame_us\n";
  for (const auto& r : copy) s << r.ms << ',' << r.epoch << ',' << r.frame << ','
      << r.units << ',' << r.fresh << ',' << r.source_fps << ',' << r.multiplier << ','
      << r.queue_us << ',' << r.pipeline_us << ',' << r.gpu_us << ',' << r.ai_us << ','
      << r.proposed_cap << ',' << r.accepted_limit_us << ',' << r.vsync << ',' << r.dynamic << ','
      << r.mode << ',' << r.previous_frame << ',' << r.previous_gpu_end << ',' << r.gpu_end << ','
      << r.simulation_start << ',' << r.present_start << ',' << r.qpc_frequency << ','
      << r.raw_gpu_frame_us << '\n';
  return s.str();
}
}
