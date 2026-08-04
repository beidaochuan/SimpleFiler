#pragma once

#include <functional>
#include <string>

namespace sf {

// Builds "name - コピー.ext" (index == 1) or "name - コピー (index).ext"
// (index >= 2). When isDirectory is true, the name is not split at an
// extension. Performs no filesystem access.
[[nodiscard]] std::wstring BuildDuplicateName(const std::wstring &originalName,
                                              bool isDirectory, int index);

// Returns the first candidate produced by BuildDuplicateName for which
// nameExists returns false. Callers decide what "exists" means (disk
// presence, names already reserved in the same batch, ...).
[[nodiscard]] std::wstring GenerateDuplicateName(
    const std::wstring &originalName, bool isDirectory,
    const std::function<bool(const std::wstring &)> &nameExists);

} // namespace sf
