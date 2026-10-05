#include <cstdlib>
#include <iostream>

#include "../src/addons/mfgunlock/input_diagnostics.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition       \
                << '\n';                                                       \
      return EXIT_FAILURE;                                                      \
    }                                                                           \
  } while (false)

int main() {
  using namespace mfgunlock::inputdiag;
  Clear();
  g_enabled.store(true, std::memory_order_release);
  g_native_api.store(NativeApi::Unknown, std::memory_order_release);
  ObserveOutput(1920, 1080, 24, 1);
  OutputSnapshot output{};
  CHECK(SnapshotOutput(output));
  CHECK(output.samples == 1 && output.changes == 0);
  CHECK(output.width == 1920 && output.format == 24 && output.color_space == 1);
  ObserveOutput(1920, 1080, 24, 3);
  CHECK(SnapshotOutput(output));
  CHECK(output.samples == 2 && output.changes == 1 && output.color_space == 3);

  sl::Resource color(sl::ResourceType::eTex2d,
                     reinterpret_cast<void*>(uintptr_t{0x1000}));
  color.width = 1920;
  color.height = 1080;
  color.nativeFormat = 10;
  sl::Resource motion(sl::ResourceType::eTex2d,
                      reinterpret_cast<void*>(uintptr_t{0x2000}));
  motion.width = 960;
  motion.height = 540;
  motion.nativeFormat = 16;
  sl::Resource hudless(sl::ResourceType::eTex2d,
                       reinterpret_cast<void*>(uintptr_t{0x3000}));
  hudless.width = 1920;
  hudless.height = 1080;
  hudless.nativeFormat = 10;

  sl::ResourceTag tags[] = {
      {&color, sl::kBufferTypeBackbuffer, sl::eValidUntilPresent},
      {&motion, sl::kBufferTypeMotionVectors, sl::eValidUntilPresent},
      {&hudless, sl::kBufferTypeHUDLessColor, sl::eValidUntilPresent},
  };
  ObserveTags(7, tags, 3, true, 11, 1);
  std::array<ViewportSnapshot, kMaxViewports> snapshots{};
  CHECK(SnapshotAll(snapshots) == 1);
  const auto& first = snapshots[0];
  CHECK(first.viewport == 7 && first.format_api == 1);
  CHECK(first.tag_batches == 1 && first.frame_aware_batches == 1);
  CHECK(first.tags[static_cast<size_t>(Kind::Backbuffer)].native_format == 10);
  CHECK(!first.tags[static_cast<size_t>(Kind::Backbuffer)].native_desc_seen);
  CHECK(first.tags[static_cast<size_t>(Kind::MotionVectors)].width == 960);
  CHECK(first.tags[static_cast<size_t>(Kind::Hudless)].last_frame == 11);

  sl::DLSSGOptions game_options{};
  game_options.mode = sl::DLSSGMode::eOn;
  game_options.numFramesToGenerate = 3;
  game_options.mvecDepthWidth = 1280;
  game_options.mvecDepthHeight = 720;
  game_options.colorWidth = 1920;
  game_options.colorHeight = 1080;
  game_options.colorBufferFormat = 24;
  game_options.mvecBufferFormat = 34;
  game_options.depthBufferFormat = 19;
  game_options.hudLessBufferFormat = 24;
  game_options.uiBufferFormat = 29;
  game_options.enableUserInterfaceRecomposition = sl::Boolean::eFalse;
  ObserveOptions(7, game_options, false);
  sl::DLSSGOptions forwarded_options = game_options;
  forwarded_options.enableUserInterfaceRecomposition = sl::Boolean::eTrue;
  ObserveOptions(7, forwarded_options, true);
  CHECK(SnapshotAll(snapshots) == 1);
  CHECK(snapshots[0].game_options.calls == 1);
  CHECK(snapshots[0].game_options.generated_frames == 3);
  CHECK(snapshots[0].game_options.ui_format == 29);
  CHECK(snapshots[0].game_options.ui_recomposition == 0);
  CHECK(snapshots[0].forwarded_options.calls == 1);
  CHECK(snapshots[0].forwarded_options.ui_recomposition == 1);
  forwarded_options.numFramesToGenerate = 2;
  ObserveOptions(7, forwarded_options, true);
  CHECK(SnapshotAll(snapshots) == 1);
  CHECK(snapshots[0].forwarded_options.calls == 2);
  CHECK(snapshots[0].forwarded_options.changes == 1);
  CHECK(snapshots[0].forwarded_options.generated_frames == 2);

  hudless.native = reinterpret_cast<void*>(uintptr_t{0x3010});
  hudless.width = 1600;
  ObserveTags(7, &tags[2], 1, true, 12, 1);
  CHECK(SnapshotAll(snapshots) == 1);
  const auto& changed = snapshots[0].tags[static_cast<size_t>(Kind::Hudless)];
  CHECK(changed.sets == 2 && changed.native_identity_changes == 1);
  CHECK(changed.metadata_changes == 1 && changed.width == 1600);

  tags[2].resource = nullptr;
  ObserveTags(7, &tags[2], 1, true, 13, 1);
  CHECK(SnapshotAll(snapshots) == 1);
  const auto& cleared = snapshots[0].tags[static_cast<size_t>(Kind::Hudless)];
  CHECK(cleared.clears == 1 && !cleared.has_resource);

  tags[2].resource = &hudless;
  tags[2].extent = {0, 100, 1600, 1080};
  ObserveTags(7, &tags[2], 1, true, 14, 1);
  CHECK(SnapshotAll(snapshots) == 1);
  CHECK(snapshots[0].tags[static_cast<size_t>(Kind::Hudless)]
            .invalid_base_metadata == 1);

  sl::Constants constants{};
  constants.reset = sl::Boolean::eTrue;
  constants.motionVectorsJittered = sl::Boolean::eFalse;
  constants.mvecScale = {1.0f, 1.0f};
  constants.jitterOffset = {0.25f, -0.25f};
  ObserveConstants(7, constants, 14);
  CHECK(SnapshotAll(snapshots) == 1);
  CHECK(snapshots[0].constants.calls == 1);
  CHECK(snapshots[0].constants.game_reset_true == 1);
  CHECK(snapshots[0].constants.mvec_scale_x == 1.0f);

  g_enabled.store(false, std::memory_order_release);
  ObserveTags(7, tags, 3, true, 15, 1);
  CHECK(SnapshotAll(snapshots) == 1 && snapshots[0].tag_batches == 4);
  Clear();
  CHECK(SnapshotAll(snapshots) == 1 && snapshots[0].tag_batches == 0);
  CHECK(!SnapshotOutput(output));
  g_enabled.store(true, std::memory_order_release);
  ObserveConstants(9, constants, 20);
  CHECK(SnapshotAll(snapshots) == 2);
  CHECK(snapshots[1].viewport == 9 && snapshots[1].constants.calls == 1);
  Clear();
  CHECK(g_enabled.load(std::memory_order_acquire));
  CHECK(SnapshotAll(snapshots) == 2 && snapshots[1].constants.calls == 0);
  g_enabled.store(false, std::memory_order_release);
  std::cout << "input diagnostics tests passed\n";
  return EXIT_SUCCESS;
}
