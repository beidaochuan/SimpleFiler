#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sf {

struct AppArgumentContext final {
  std::vector<std::wstring> files;
  std::wstring folder;
  std::wstring left;
  std::wstring right;
  std::wstring other;
};

struct AppArgumentExpansion final {
  std::wstring commandLine;
  std::wstring error;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

// Quotes one argument according to the Windows CommandLineToArgvW/CRT parsing
// rules. Arguments are quoted only when required; embedded quotes and trailing
// backslashes are escaped.
[[nodiscard]] std::wstring
QuoteWindowsCommandLineArgument(std::wstring_view argument);

// Expands {file}, {files}, {folder}, {left}, {right}, and {other}. Every
// substituted value is independently command-line quoted. {file} is the first
// selected file and {files} is all selected files separated by spaces.
//
// Unknown placeholders and placeholders without a value are rejected: the
// result contains an empty commandLine and a non-empty error. Use {{ and }} for
// literal braces.
[[nodiscard]] AppArgumentExpansion
ExpandAppArgumentTemplate(std::wstring_view argumentTemplate,
                          const AppArgumentContext &context);

} // namespace sf
