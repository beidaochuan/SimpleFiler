#include "core/CommandQuery.h"

#include <algorithm>
#include <cwctype>

namespace sf {
namespace {

wchar_t Fold(wchar_t value) {
  return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(value)));
}

bool StartsWithAsciiInsensitive(std::wstring_view value,
                                std::wstring_view prefix) {
  if (value.size() < prefix.size())
    return false;
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    if (Fold(value[index]) != Fold(prefix[index]))
      return false;
  }
  return true;
}

std::wstring Trim(std::wstring_view value) {
  while (!value.empty() && std::iswspace(static_cast<wint_t>(value.front())))
    value.remove_prefix(1);
  while (!value.empty() && std::iswspace(static_cast<wint_t>(value.back())))
    value.remove_suffix(1);
  return std::wstring(value);
}

std::wstring Folded(std::wstring_view value) {
  std::wstring result(value);
  std::transform(result.begin(), result.end(), result.begin(), Fold);
  return result;
}

} // namespace

CommandQuery ParseCommandQuery(std::wstring_view input) {
  while (!input.empty() &&
         std::iswspace(static_cast<wint_t>(input.front()))) {
    input.remove_prefix(1);
  }

  if (StartsWithAsciiInsensitive(input, L"ff")) {
    return {CommandMode::Folder, Trim(input.substr(2))};
  }
  if (StartsWithAsciiInsensitive(input, L"aa")) {
    return {CommandMode::Application, Trim(input.substr(2))};
  }
  if (StartsWithAsciiInsensitive(input, L"cmd") &&
      (input.size() == 3 ||
       std::iswspace(static_cast<wint_t>(input[3])) != 0)) {
    return {CommandMode::Terminal, Trim(input.substr(3))};
  }
  return {};
}

std::optional<int> CommandMatchScore(std::wstring_view query,
                                     std::wstring_view candidate) {
  const std::wstring foldedQuery = Folded(query);
  const std::wstring foldedCandidate = Folded(candidate);
  if (foldedQuery.empty())
    return 0;
  if (foldedCandidate == foldedQuery)
    return 4000;
  if (foldedCandidate.starts_with(foldedQuery)) {
    return 3000 - static_cast<int>(
                      std::min<std::size_t>(foldedCandidate.size(), 500));
  }
  const std::size_t substring = foldedCandidate.find(foldedQuery);
  if (substring != std::wstring::npos) {
    return 2000 - static_cast<int>(std::min<std::size_t>(substring, 500));
  }

  std::size_t queryIndex = 0;
  std::size_t first = 0;
  std::size_t last = 0;
  for (std::size_t candidateIndex = 0;
       candidateIndex < foldedCandidate.size() &&
       queryIndex < foldedQuery.size();
       ++candidateIndex) {
    if (foldedCandidate[candidateIndex] == foldedQuery[queryIndex]) {
      if (queryIndex == 0)
        first = candidateIndex;
      last = candidateIndex;
      ++queryIndex;
    }
  }
  if (queryIndex != foldedQuery.size())
    return std::nullopt;

  const std::size_t span = last - first + 1;
  const std::size_t gaps = span - foldedQuery.size();
  return 1000 - static_cast<int>(
                    std::min<std::size_t>(first + gaps * 2, 900));
}

} // namespace sf
