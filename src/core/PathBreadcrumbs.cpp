#include "core/PathBreadcrumbs.h"

#include <cstddef>
#include <utility>

namespace sf {
namespace {

bool IsSeparator(wchar_t value) noexcept {
  return value == L'\\' || value == L'/';
}

void AppendComponents(std::vector<PathBreadcrumb> &result,
                      std::wstring_view path, std::size_t offset,
                      std::wstring target) {
  while (offset < path.size()) {
    while (offset < path.size() && IsSeparator(path[offset]))
      ++offset;
    if (offset == path.size())
      break;

    const std::size_t end = path.find_first_of(L"\\/", offset);
    const std::wstring component(
        path.substr(offset, end == std::wstring_view::npos
                                ? path.size() - offset
                                : end - offset));
    if (!target.empty() && !IsSeparator(target.back()))
      target.push_back(L'\\');
    target += component;
    result.push_back({component, target});
    if (end == std::wstring_view::npos)
      break;
    offset = end + 1;
  }
}

} // namespace

std::vector<PathBreadcrumb>
BuildPathBreadcrumbs(const std::wstring_view inputPath) {
  constexpr std::wstring_view searchPrefix = L"検索: ";
  const std::wstring_view path =
      inputPath.starts_with(searchPrefix) ? inputPath.substr(searchPrefix.size())
                                          : inputPath;

  std::vector<PathBreadcrumb> result{{L"PC", {}}};
  if (path.empty() || path == L"PC")
    return result;

  if (path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1])) {
    std::size_t offset = 2;
    const std::size_t serverEnd = path.find_first_of(L"\\/", offset);
    if (serverEnd == std::wstring_view::npos) {
      const std::wstring server(path.substr(offset));
      if (!server.empty())
        result.push_back({L"\\\\" + server, L"\\\\" + server});
      return result;
    }

    const std::wstring server(path.substr(offset, serverEnd - offset));
    std::wstring target = L"\\\\";
    target += server;
    if (!server.empty())
      result.push_back({L"\\\\" + server, target});
    AppendComponents(result, path, serverEnd + 1, std::move(target));
    return result;
  }

  if (path.size() >= 2 && path[1] == L':') {
    std::wstring root(path.substr(0, 2));
    root.push_back(L'\\');
    result.push_back({std::wstring(path.substr(0, 2)), root});
    AppendComponents(result, path, 2, std::move(root));
    return result;
  }

  AppendComponents(result, path, 0, {});
  return result;
}

} // namespace sf
