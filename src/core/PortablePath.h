#pragma once

#include <filesystem>

namespace sf {

// Resolves a path read from portable settings. Relative paths are interpreted
// relative to applicationDirectory. The operation is lexical and does not
// require either path to exist.
[[nodiscard]] std::filesystem::path
ResolvePortablePath(const std::filesystem::path &storedPath,
                    const std::filesystem::path &applicationDirectory);

// Converts an absolute path to a portable, relative path only when it is
// lexically contained by applicationDirectory. Paths outside the application
// directory remain absolute. The operation does not access the filesystem.
[[nodiscard]] std::filesystem::path
MakePortablePath(const std::filesystem::path &absolutePath,
                 const std::filesystem::path &applicationDirectory);

} // namespace sf
