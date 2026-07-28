#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sf {

enum class CommandMode {
  None,
  Folder,
  Application,
  Terminal,
};

struct CommandQuery final {
  CommandMode mode = CommandMode::None;
  std::wstring query;
};

// Parses the command prefixes used by the keyboard-first launcher.
// Prefixes are ASCII case-insensitive. Whitespace after a prefix is optional.
[[nodiscard]] CommandQuery ParseCommandQuery(std::wstring_view input);

// Returns a larger score for a better case-insensitive match. Exact, prefix,
// substring and subsequence matches are ranked in that order.
[[nodiscard]] std::optional<int>
CommandMatchScore(std::wstring_view query, std::wstring_view candidate);

} // namespace sf
