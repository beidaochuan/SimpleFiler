#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace sf::win {

struct DriveCapacity final {
  std::uint64_t freeBytes = 0;
  std::uint64_t totalBytes = 0;
};

[[nodiscard]] std::optional<DriveCapacity>
QueryDriveCapacity(const std::wstring &path);
[[nodiscard]] std::wstring FormatDriveCapacity(const DriveCapacity &capacity);

} // namespace sf::win
