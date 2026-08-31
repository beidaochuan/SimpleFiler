#include "win/DriveInfo.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <optional>

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

// Finds a drive letter that Windows reports as unused, so tests can probe
// GetDiskFreeSpaceExW's failure path without depending on a specific letter
// being free in every environment.
std::optional<wchar_t> FindUnusedDriveLetter() {
  const DWORD usedDrives = GetLogicalDrives();
  for (wchar_t letter = L'Z'; letter >= L'A'; --letter) {
    if ((usedDrives & (1U << (letter - L'A'))) == 0)
      return letter;
  }
  return std::nullopt;
}

void TestFormatDriveCapacity() {
  Check(sf::win::FormatDriveCapacity({0, 0}) == L"空き 0 B / 合計 0 B",
        "Zero capacity should format using bytes");
  Check(sf::win::FormatDriveCapacity(
            {5ULL * 1024 * 1024 * 1024, 500ULL * 1024 * 1024 * 1024}) ==
            L"空き 5.0 GB / 合計 500.0 GB",
        "Gibibyte values should reuse FormatFileSize's GB formatting");
}

void TestQueryDriveCapacity() {
  Check(!sf::win::QueryDriveCapacity(L"").has_value(),
        "An empty path should not resolve to a drive");
  Check(!sf::win::QueryDriveCapacity(L"relative\\path").has_value(),
        "A path with no drive root should not resolve to a drive");

  if (const auto unusedLetter = FindUnusedDriveLetter()) {
    const std::wstring path =
        std::wstring(1, *unusedLetter) + L":\\nonexistent\\";
    Check(!sf::win::QueryDriveCapacity(path).has_value(),
          "A drive letter with no root directory should return nullopt");
  }

  const auto capacity =
      sf::win::QueryDriveCapacity(std::filesystem::temp_directory_path()
                                       .wstring());
  Check(capacity.has_value(),
        "The temp directory's drive should report a capacity");
  if (capacity)
    Check(capacity->totalBytes >= capacity->freeBytes,
          "Total capacity should never be smaller than free capacity");
}

} // namespace

int main() {
  TestFormatDriveCapacity();
  TestQueryDriveCapacity();

  if (failures == 0)
    std::cout << "DriveInfo tests passed\n";
  return failures == 0 ? 0 : 1;
}
