#include "core/PortablePath.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <string>

namespace sf {
namespace {

std::filesystem::path Normalize(const std::filesystem::path &path) {
  std::filesystem::path normalized = path.lexically_normal();
  while (normalized != normalized.root_path() && normalized.filename().empty())
    normalized = normalized.parent_path();
  return normalized;
}

bool ComponentEquals(const std::filesystem::path &left,
                     const std::filesystem::path &right) {
#ifdef _WIN32
  const std::wstring leftValue = left.native();
  const std::wstring rightValue = right.native();
  return CompareStringOrdinal(
             leftValue.data(), static_cast<int>(leftValue.size()),
             rightValue.data(), static_cast<int>(rightValue.size()), TRUE) ==
         CSTR_EQUAL;
#else
  return left == right;
#endif
}

bool HasEquivalentRoot(const std::filesystem::path &left,
                       const std::filesystem::path &right) {
  return ComponentEquals(left.root_name(), right.root_name()) &&
         ComponentEquals(left.root_directory(), right.root_directory());
}

std::filesystem::path RelativePathIfContained(
    const std::filesystem::path &path,
    const std::filesystem::path &applicationDirectory) {
  if (!path.is_absolute() || !applicationDirectory.is_absolute() ||
      !HasEquivalentRoot(path, applicationDirectory)) {
    return {};
  }

  const std::filesystem::path pathRelative = path.relative_path();
  const std::filesystem::path directoryRelative =
      applicationDirectory.relative_path();
  auto pathPart = pathRelative.begin();
  const auto pathEnd = pathRelative.end();
  auto directoryPart = directoryRelative.begin();
  const auto directoryEnd = directoryRelative.end();

  for (; directoryPart != directoryEnd; ++directoryPart, ++pathPart) {
    if (pathPart == pathEnd || !ComponentEquals(*pathPart, *directoryPart))
      return {};
  }

  std::filesystem::path relative;
  for (; pathPart != pathEnd; ++pathPart)
    relative /= *pathPart;

  return relative.empty() ? std::filesystem::path(L".") : relative;
}

} // namespace

std::filesystem::path
ResolvePortablePath(const std::filesystem::path &storedPath,
                    const std::filesystem::path &applicationDirectory) {
  if (storedPath.empty())
    return {};

  const std::filesystem::path normalizedStored = Normalize(storedPath);
  if (normalizedStored.is_absolute() || normalizedStored.has_root_path())
    return normalizedStored;

  if (applicationDirectory.empty())
    return normalizedStored;

  return Normalize(Normalize(applicationDirectory) / normalizedStored);
}

std::filesystem::path
MakePortablePath(const std::filesystem::path &absolutePath,
                 const std::filesystem::path &applicationDirectory) {
  if (absolutePath.empty())
    return {};

  const std::filesystem::path normalizedPath = Normalize(absolutePath);
  if (!normalizedPath.is_absolute())
    return normalizedPath;

  if (applicationDirectory.empty())
    return normalizedPath;

  const std::filesystem::path normalizedDirectory =
      Normalize(applicationDirectory);
  const std::filesystem::path relative =
      RelativePathIfContained(normalizedPath, normalizedDirectory);
  return relative.empty() ? normalizedPath : relative;
}

} // namespace sf
