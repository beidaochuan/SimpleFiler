#include "core/DuplicateName.h"

namespace sf {
namespace {

void SplitNameExtension(const std::wstring &name, bool isDirectory,
                        std::wstring &stem, std::wstring &extension) {
  if (isDirectory) {
    stem = name;
    extension.clear();
    return;
  }
  const std::size_t dot = name.find_last_of(L'.');
  if (dot == std::wstring::npos || dot == 0) {
    stem = name;
    extension.clear();
  } else {
    stem = name.substr(0, dot);
    extension = name.substr(dot);
  }
}

} // namespace

std::wstring BuildDuplicateName(const std::wstring &originalName,
                                bool isDirectory, int index) {
  std::wstring stem;
  std::wstring extension;
  SplitNameExtension(originalName, isDirectory, stem, extension);
  std::wstring suffix = L" - コピー";
  if (index >= 2)
    suffix += L" (" + std::to_wstring(index) + L")";
  return stem + suffix + extension;
}

std::wstring GenerateDuplicateName(
    const std::wstring &originalName, bool isDirectory,
    const std::function<bool(const std::wstring &)> &nameExists) {
  constexpr int kMaxAttempts = 10000;
  for (int index = 1; index <= kMaxAttempts; ++index) {
    std::wstring candidate = BuildDuplicateName(originalName, isDirectory, index);
    if (!nameExists(candidate))
      return candidate;
  }
  return BuildDuplicateName(originalName, isDirectory, kMaxAttempts + 1);
}

} // namespace sf
