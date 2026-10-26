#include <cstdlib>
#include <iostream>
#include <string>

#include "../src/addons/mfgunlock/module_scan.hpp"

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
                << '\n';                                                        \
      std::abort();                                                              \
    }                                                                           \
  } while (false)

using namespace mfgunlock::modulescan;

// Stored in this executable's read-only data.
extern "C" __declspec(dllexport) const char kEmbeddedMarker[] = "module-scan embedded marker 7f3a";

int main() {
  const unsigned char data[] = "..FG1 DLL has been detected..";
  const auto size = sizeof(data) - 1;
  CHECK(ContainsBytes(data, size, "FG1 DLL has been detected", 25));
  CHECK(ContainsBytes(data, size, "..", 2));                    // at the start
  CHECK(ContainsBytes(data, size, "ted..", 5));                 // at the end
  CHECK(!ContainsBytes(data, size, "FG2 DLL", 7));
  CHECK(!ContainsBytes(data, 3, "FG1 DLL has been detected", 25));  // shorter than the needle
  CHECK(!ContainsBytes(data, size, "", 0));
  CHECK(!ContainsBytes(nullptr, size, "FG1", 3));

  // The mapped image of this test.
  const HMODULE self = GetModuleHandleW(nullptr);
  CHECK(ImageContains(self, kEmbeddedMarker, sizeof(kEmbeddedMarker) - 1));
  std::string absent = "module-scan absent marker ";
  absent += std::to_string(GetCurrentProcessId() ^ 0x5a5a5a5a);  // built at run time, not in the image
  CHECK(!ImageContains(self, absent.c_str(), absent.size()));
  CHECK(!ImageContains(nullptr, kEmbeddedMarker, 5));
  int stack_value = 0;
  CHECK(!ImageContains(reinterpret_cast<HMODULE>(&stack_value), kEmbeddedMarker, 5));  // not an image

  ImageIdentity identity;
  CHECK(ReadIdentity(self, identity));
  CHECK(identity.module == self && identity.image_size > 0);

  // Searched images are remembered by address, timestamp and size: another
  // module mapped later at the same address is searched again.
  SearchedImages searched;
  CHECK(!searched.Contains(identity));
  searched.Add(identity);
  searched.Add(identity);
  CHECK(searched.Contains(identity) && searched.size() == 1);
  auto remapped = identity;
  remapped.timestamp ^= 1;
  CHECK(!searched.Contains(remapped));
  remapped = identity;
  remapped.image_size += 4096;
  CHECK(!searched.Contains(remapped));

  std::cout << "module_scan_tests passed\n";
  return 0;
}
