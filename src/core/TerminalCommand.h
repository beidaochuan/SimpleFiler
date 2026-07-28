#pragma once

#include <string>

namespace sf {

[[nodiscard]] std::string Base64Encode(const std::string &bytes);
[[nodiscard]] std::string
Utf8ToPowerShellEncodedCommand(const std::string &utf8Path);
[[nodiscard]] std::wstring EscapeCmdUncPath(const std::wstring &path);
[[nodiscard]] std::wstring BuildCmdUncParameters(const std::wstring &path);

} // namespace sf
