#include "core/TerminalCommand.h"

#include <cstdint>
#include <vector>

namespace sf {
namespace {

std::vector<std::uint8_t> Utf8ToUtf16LeBytes(const std::string &input) {
  std::vector<std::uint8_t> output;
  for (std::size_t index = 0; index < input.size();) {
    std::uint32_t point = static_cast<unsigned char>(input[index++]);
    if ((point & 0x80U) != 0U) {
      std::uint32_t minimum = 0;
      int continuationCount = 0;
      if ((point & 0xE0U) == 0xC0U) {
        point &= 0x1FU;
        continuationCount = 1;
        minimum = 0x80U;
      } else if ((point & 0xF0U) == 0xE0U) {
        point &= 0x0FU;
        continuationCount = 2;
        minimum = 0x800U;
      } else if ((point & 0xF8U) == 0xF0U) {
        point &= 0x07U;
        continuationCount = 3;
        minimum = 0x10000U;
      } else {
        point = 0xFFFDU;
      }
      for (int part = 0; part < continuationCount; ++part) {
        if (index >= input.size() ||
            (static_cast<unsigned char>(input[index]) & 0xC0U) != 0x80U) {
          point = 0xFFFDU;
          break;
        }
        point = (point << 6U) |
                (static_cast<unsigned char>(input[index++]) & 0x3FU);
      }
      if (point < minimum || point > 0x10FFFFU ||
          (point >= 0xD800U && point <= 0xDFFFU)) {
        point = 0xFFFDU;
      }
    }
    const auto appendUnit = [&output](std::uint16_t unit) {
      output.push_back(static_cast<std::uint8_t>(unit & 0xFFU));
      output.push_back(static_cast<std::uint8_t>(unit >> 8U));
    };
    if (point <= 0xFFFFU) {
      appendUnit(static_cast<std::uint16_t>(point));
    } else {
      point -= 0x10000U;
      appendUnit(static_cast<std::uint16_t>(0xD800U + (point >> 10U)));
      appendUnit(static_cast<std::uint16_t>(0xDC00U + (point & 0x3FFU)));
    }
  }
  return output;
}

} // namespace

std::string Base64Encode(const std::string &bytes) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((bytes.size() + 2U) / 3U) * 4U);
  for (std::size_t index = 0; index < bytes.size(); index += 3) {
    const std::uint32_t a = static_cast<unsigned char>(bytes[index]);
    const std::uint32_t b = index + 1 < bytes.size()
                                ? static_cast<unsigned char>(bytes[index + 1])
                                : 0U;
    const std::uint32_t c = index + 2 < bytes.size()
                                ? static_cast<unsigned char>(bytes[index + 2])
                                : 0U;
    const std::uint32_t combined = (a << 16U) | (b << 8U) | c;
    output.push_back(alphabet[(combined >> 18U) & 63U]);
    output.push_back(alphabet[(combined >> 12U) & 63U]);
    output.push_back(index + 1 < bytes.size() ? alphabet[(combined >> 6U) & 63U]
                                              : '=');
    output.push_back(index + 2 < bytes.size() ? alphabet[combined & 63U] : '=');
  }
  return output;
}

std::string Utf8ToPowerShellEncodedCommand(const std::string &utf8Path) {
  const std::string pathBase64 = Base64Encode(utf8Path);
  const std::string script =
      "$p=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" +
      pathBase64 + "'));Set-Location -LiteralPath $p";
  const std::vector<std::uint8_t> bytes = Utf8ToUtf16LeBytes(script);
  return Base64Encode(
      std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

std::wstring EscapeCmdUncPath(const std::wstring &path) {
  // Windows file names cannot contain a quote. CMD metacharacters are literal
  // while the path is inside the inner quote pair.
  return path;
}

std::wstring BuildCmdUncParameters(const std::wstring &path) {
  // /S strips the outer quote pair and leaves the inner pair around the path.
  return L"/D /S /K \"pushd \"" + EscapeCmdUncPath(path) + L"\"\"";
}

} // namespace sf
