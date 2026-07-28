#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace sf::win {

inline std::string WideToUtf8(const std::wstring &value) {
  if (value.empty())
    return {};
  const int count = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (count <= 0)
    return {};
  std::string result(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), count,
                      nullptr, nullptr);
  return result;
}

inline std::wstring Utf8ToWide(const std::string &value) {
  if (value.empty())
    return {};
  const int count =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0)
    return {};
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), count);
  return result;
}

inline std::wstring WindowsErrorMessage(DWORD error) {
  wchar_t *buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
  std::wstring result = length != 0 && buffer != nullptr
                            ? std::wstring(buffer, length)
                            : L"不明なエラー";
  if (buffer != nullptr)
    LocalFree(buffer);
  while (!result.empty() &&
         (result.back() == L'\r' || result.back() == L'\n')) {
    result.pop_back();
  }
  return result;
}

inline std::filesystem::path ExecutablePath() {
  std::vector<wchar_t> buffer(512);
  while (true) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0)
      return {};
    if (length < buffer.size() - 1) {
      return std::filesystem::path(std::wstring(buffer.data(), length));
    }
    buffer.resize(buffer.size() * 2U);
  }
}

inline std::wstring GetWindowTextString(HWND window) {
  const int length = GetWindowTextLengthW(window);
  std::wstring value(static_cast<std::size_t>(length + 1), L'\0');
  if (length > 0)
    GetWindowTextW(window, value.data(), length + 1);
  value.resize(static_cast<std::size_t>(length));
  return value;
}

inline std::wstring ToExtendedPath(const std::wstring &path) {
  if (path.rfind(LR"(\\?\)", 0) == 0)
    return path;
  if (path.rfind(LR"(\\)", 0) == 0)
    return LR"(\\?\UNC\)" + path.substr(2);
  if (path.size() >= 3 && path[1] == L':' &&
      (path[2] == L'\\' || path[2] == L'/'))
    return LR"(\\?\)" + path;
  return path;
}

inline bool IsDirectory(const std::wstring &path) {
  const std::wstring extended = ToExtendedPath(path);
  const DWORD attributes = GetFileAttributesW(extended.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline bool IsUncPath(const std::wstring &path) {
  return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
}

inline std::wstring QuoteArgument(const std::wstring &argument) {
  if (argument.find_first_of(L" \t\"") == std::wstring::npos)
    return argument;
  std::wstring output = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t ch : argument) {
    if (ch == L'\\') {
      ++backslashes;
    } else if (ch == L'"') {
      output.append(backslashes * 2U + 1U, L'\\');
      output.push_back(L'"');
      backslashes = 0;
    } else {
      output.append(backslashes, L'\\');
      backslashes = 0;
      output.push_back(ch);
    }
  }
  output.append(backslashes * 2U, L'\\');
  output.push_back(L'"');
  return output;
}

} // namespace sf::win
