#include "win/DriveInfo.h"

#include "win/FileEnumerator.h"

#include <windows.h>

#include <filesystem>
#include <format>

namespace sf::win {

std::optional<DriveCapacity> QueryDriveCapacity(const std::wstring &path) {
  if (path.empty())
    return std::nullopt;
  const std::wstring root = std::filesystem::path(path).root_path().wstring();
  if (root.empty())
    return std::nullopt;
  // GetDiskFreeSpaceExW can block for a long time on a drive with no media
  // (an empty optical drive) since it has to poll the hardware; skip those
  // instead of risking a UI freeze from the timer-driven caller.
  const UINT driveType = GetDriveTypeW(root.c_str());
  if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_CDROM)
    return std::nullopt;
  ULARGE_INTEGER freeBytes{};
  ULARGE_INTEGER totalBytes{};
  if (!GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr))
    return std::nullopt;
  return DriveCapacity{freeBytes.QuadPart, totalBytes.QuadPart};
}

std::wstring FormatDriveCapacity(const DriveCapacity &capacity) {
  return std::format(L"空き {} / 合計 {}", FormatFileSize(capacity.freeBytes),
                     FormatFileSize(capacity.totalBytes));
}

} // namespace sf::win
