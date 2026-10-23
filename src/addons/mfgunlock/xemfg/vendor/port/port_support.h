#pragma once
#include "../xess_mod.h"
#include "../runtime_layout.h"
#include "../sha256_util.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static std::string escape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c >= 32) out += char(c);
  }
  return out;
}
static void logEvent(const char* stage, const std::string& fields = "") {
  xess_mod::WriteLog(stage, fields);
}
