#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct PathBreadcrumb final {
  std::wstring label;
  std::wstring path;
};

// Converts a Windows path into address-bar breadcrumb entries. The first
// entry is always PC and uses an empty path to represent the drive view.
[[nodiscard]] std::vector<PathBreadcrumb>
BuildPathBreadcrumbs(std::wstring_view path);

} // namespace sf
