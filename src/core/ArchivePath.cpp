#include "core/ArchivePath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace sf {
namespace {

bool IsReservedWindowsName(std::string component) {
  const std::size_t dot = component.find('.');
  if (dot != std::string::npos)
    component.resize(dot);
  std::transform(
      component.begin(), component.end(), component.begin(),
      [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  constexpr std::array<std::string_view, 4> names{"CON", "PRN", "AUX", "NUL"};
  if (std::find(names.begin(), names.end(), component) != names.end())
    return true;
  if (component.size() == 4 &&
      (component.rfind("COM", 0) == 0 || component.rfind("LPT", 0) == 0) &&
      component[3] >= '1' && component[3] <= '9') {
    return true;
  }
  return false;
}

} // namespace

bool IsSafeArchivePath(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\' ||
      path.find(':') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) {
    return false;
  }

  std::size_t start = 0;
  while (start < path.size()) {
    const std::size_t end = path.find_first_of("/\\", start);
    const std::size_t length =
        end == std::string_view::npos ? path.size() - start : end - start;
    const std::string component(path.substr(start, length));
    if (component.empty() || component == "." || component == ".." ||
        component.back() == '.' || component.back() == ' ' ||
        IsReservedWindowsName(component)) {
      return false;
    }
    if (end == std::string_view::npos)
      return true;
    start = end + 1;
    if (start == path.size())
      return true; // A single trailing slash is a valid directory entry.
  }
  return true;
}

} // namespace sf
