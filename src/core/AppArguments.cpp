#include "core/AppArguments.h"

#include <cwctype>

namespace sf {
namespace {

bool NeedsQuotes(std::wstring_view argument) {
  if (argument.empty())
    return true;
  for (const wchar_t value : argument) {
    if (std::iswspace(static_cast<wint_t>(value)) != 0 || value == L'"')
      return true;
  }
  return false;
}

AppArgumentExpansion Failure(std::wstring message) {
  return {{}, std::move(message)};
}

AppArgumentExpansion MissingValue(std::wstring_view placeholder) {
  std::wstring message = L"Placeholder {";
  message.append(placeholder);
  message.append(L"} has no value");
  return Failure(std::move(message));
}

void AppendQuoted(std::wstring &destination, std::wstring_view value) {
  destination.append(QuoteWindowsCommandLineArgument(value));
}

} // namespace

std::wstring QuoteWindowsCommandLineArgument(std::wstring_view argument) {
  if (!NeedsQuotes(argument))
    return std::wstring(argument);

  std::wstring result;
  result.reserve(argument.size() + 2);
  result.push_back(L'"');

  std::size_t backslashes = 0;
  for (const wchar_t value : argument) {
    if (value == L'\\') {
      ++backslashes;
      continue;
    }

    if (value == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(L'"');
    } else {
      result.append(backslashes, L'\\');
      result.push_back(value);
    }
    backslashes = 0;
  }

  // Backslashes immediately before the closing quote must be doubled.
  result.append(backslashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

AppArgumentExpansion
ExpandAppArgumentTemplate(std::wstring_view argumentTemplate,
                          const AppArgumentContext &context) {
  std::wstring result;
  result.reserve(argumentTemplate.size() + 32);

  for (std::size_t index = 0; index < argumentTemplate.size();) {
    if (argumentTemplate[index] == L'{' &&
        index + 1 < argumentTemplate.size() &&
        argumentTemplate[index + 1] == L'{') {
      result.push_back(L'{');
      index += 2;
      continue;
    }
    if (argumentTemplate[index] == L'}' &&
        index + 1 < argumentTemplate.size() &&
        argumentTemplate[index + 1] == L'}') {
      result.push_back(L'}');
      index += 2;
      continue;
    }
    if (argumentTemplate[index] != L'{') {
      result.push_back(argumentTemplate[index]);
      ++index;
      continue;
    }

    const std::size_t closingBrace = argumentTemplate.find(L'}', index + 1);
    if (closingBrace == std::wstring_view::npos)
      return Failure(L"Unclosed placeholder");

    const std::wstring_view placeholder =
        argumentTemplate.substr(index + 1, closingBrace - index - 1);
    if (placeholder == L"file") {
      if (context.files.empty())
        return MissingValue(placeholder);
      AppendQuoted(result, context.files.front());
    } else if (placeholder == L"files") {
      if (context.files.empty())
        return MissingValue(placeholder);
      for (std::size_t fileIndex = 0; fileIndex < context.files.size();
           ++fileIndex) {
        if (fileIndex != 0)
          result.push_back(L' ');
        AppendQuoted(result, context.files[fileIndex]);
      }
    } else {
      const std::wstring *value = nullptr;
      if (placeholder == L"folder")
        value = &context.folder;
      else if (placeholder == L"left")
        value = &context.left;
      else if (placeholder == L"right")
        value = &context.right;
      else if (placeholder == L"other")
        value = &context.other;
      else {
        std::wstring message = L"Unknown placeholder {";
        message.append(placeholder);
        message.push_back(L'}');
        return Failure(std::move(message));
      }

      if (value->empty())
        return MissingValue(placeholder);
      AppendQuoted(result, *value);
    }
    index = closingBrace + 1;
  }

  return {std::move(result), {}};
}

} // namespace sf
