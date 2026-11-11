// win_util.h - small helpers shared by the Windows platform files:
// reading the registry and releasing COM objects automatically.

#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "core/util.h"

namespace platform::win {

// Reads a text value from the registry. Returns an empty optional if the key
// or value is missing or not readable.
//
// RegGetValueW is used instead of the older RegOpenKeyEx + RegQueryValueEx
// pair because it opens, type-checks and reads in one call. RRF_RT_REG_SZ
// makes it fail if the value is not text, so we never misread a number.
// RRF_SUBKEY_WOW6464KEY forces the 64-bit view of the registry. Windows keeps
// a separate 32-bit view; asking for the 64-bit one explicitly means the
// answer does not depend on how this program was compiled.
inline std::optional<std::string> regString(HKEY root, const wchar_t* key, const wchar_t* name) {
  DWORD size = 0;
  const DWORD flags = RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY;
  // First call with no buffer: Windows only tells us how many bytes we need.
  if (RegGetValueW(root, key, name, flags, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
    return std::nullopt;
  }
  std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
  size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
  if (RegGetValueW(root, key, name, flags, nullptr, buffer.data(), &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  // The reported size includes the terminating zero. Cut the string at the
  // first zero so it does not end up inside the std::string.
  buffer.resize(wcslen(buffer.c_str()));
  return util::narrow(buffer);
}

// Same idea for a 32-bit number (REG_DWORD).
inline std::optional<DWORD> regDword(HKEY root, const wchar_t* key, const wchar_t* name) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &value,
                   &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  return value;
}

// COM objects are reference counted: whoever receives one must call Release()
// when done, or it leaks. This tiny wrapper does that in its destructor, so
// every early "return" in the security code still cleans up. It is a minimal
// version of what Microsoft::WRL::ComPtr does, written out so that it works
// the same with MSVC and MinGW and so you can read all of it.
template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ~ComPtr() {
    if (ptr_) ptr_->Release();
  }
  // COM functions hand objects back through a T** parameter. Overloading &
  // lets a ComPtr be passed directly where such a parameter is expected.
  T** operator&() { return &ptr_; }
  T* operator->() const { return ptr_; }
  T* get() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

 private:
  T* ptr_ = nullptr;
};

}
