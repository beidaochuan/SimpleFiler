#pragma once

#include <string_view>

namespace sf {

// Returns true only when a ZIP entry can be safely materialized below a
// caller-selected Windows destination directory.
[[nodiscard]] bool IsSafeArchivePath(std::string_view path);

} // namespace sf
