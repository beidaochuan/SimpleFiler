#include "core/Json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sf {
namespace {

class Parser final {
public:
  explicit Parser(const std::string &input) : input_(input) {}

  std::optional<Json> Parse(std::string *error) {
    try {
      SkipSpace();
      Json result = ParseValue();
      SkipSpace();
      if (position_ != input_.size()) {
        Fail("末尾に余分な文字があります");
      }
      return result;
    } catch (const std::runtime_error &exception) {
      if (error != nullptr) {
        *error = exception.what();
      }
      return std::nullopt;
    }
  }

private:
  Json ParseValue() {
    if (position_ >= input_.size()) {
      Fail("値がありません");
    }
    switch (input_[position_]) {
    case '{':
      return ParseObject();
    case '[':
      return ParseArray();
    case '"':
      return Json(ParseString());
    case 't':
      ConsumeLiteral("true");
      return Json(true);
    case 'f':
      ConsumeLiteral("false");
      return Json(false);
    case 'n':
      ConsumeLiteral("null");
      return Json(nullptr);
    default:
      return ParseNumber();
    }
  }

  Json ParseObject() {
    Json::Object object;
    ++position_;
    SkipSpace();
    if (Take('}')) {
      return Json(std::move(object));
    }
    while (true) {
      SkipSpace();
      if (Peek() != '"') {
        Fail("オブジェクトのキーが不正です");
      }
      std::string key = ParseString();
      SkipSpace();
      Require(':');
      SkipSpace();
      object.insert_or_assign(std::move(key), ParseValue());
      SkipSpace();
      if (Take('}')) {
        break;
      }
      Require(',');
      SkipSpace();
    }
    return Json(std::move(object));
  }

  Json ParseArray() {
    Json::Array array;
    ++position_;
    SkipSpace();
    if (Take(']')) {
      return Json(std::move(array));
    }
    while (true) {
      array.push_back(ParseValue());
      SkipSpace();
      if (Take(']')) {
        break;
      }
      Require(',');
      SkipSpace();
    }
    return Json(std::move(array));
  }

  std::string ParseString() {
    Require('"');
    std::string result;
    while (position_ < input_.size()) {
      const char ch = input_[position_++];
      if (ch == '"') {
        return result;
      }
      if (static_cast<unsigned char>(ch) < 0x20U) {
        Fail("文字列に制御文字があります");
      }
      if (ch != '\\') {
        result.push_back(ch);
        continue;
      }
      if (position_ >= input_.size()) {
        Fail("エスケープが途中で終了しました");
      }
      const char escaped = input_[position_++];
      switch (escaped) {
      case '"':
        result.push_back('"');
        break;
      case '\\':
        result.push_back('\\');
        break;
      case '/':
        result.push_back('/');
        break;
      case 'b':
        result.push_back('\b');
        break;
      case 'f':
        result.push_back('\f');
        break;
      case 'n':
        result.push_back('\n');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case 't':
        result.push_back('\t');
        break;
      case 'u':
        AppendUnicodeEscape(result);
        break;
      default:
        Fail("不正なエスケープです");
      }
    }
    Fail("文字列が閉じられていません");
  }

  void AppendUnicodeEscape(std::string &output) {
    const auto readUnit = [this]() -> std::uint32_t {
      if (position_ + 4 > input_.size()) {
        Fail("Unicodeエスケープが途中で終了しました");
      }
      std::uint32_t value = 0;
      for (int index = 0; index < 4; ++index) {
        const char ch = input_[position_++];
        value <<= 4U;
        if (ch >= '0' && ch <= '9')
          value += static_cast<unsigned>(ch - '0');
        else if (ch >= 'a' && ch <= 'f')
          value += static_cast<unsigned>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F')
          value += static_cast<unsigned>(ch - 'A' + 10);
        else
          Fail("Unicodeエスケープが不正です");
      }
      return value;
    };

    std::uint32_t codePoint = readUnit();
    if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
      if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
          input_[position_ + 1] != 'u') {
        Fail("サロゲートペアが不正です");
      }
      position_ += 2;
      const std::uint32_t low = readUnit();
      if (low < 0xDC00U || low > 0xDFFFU) {
        Fail("サロゲートペアが不正です");
      }
      codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
    } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
      Fail("サロゲートペアが不正です");
    }
    if (codePoint <= 0x7FU) {
      output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
      output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
      output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFFU) {
      output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
      output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
  }

  Json ParseNumber() {
    const std::size_t start = position_;
    if (Take('-')) {
    }
    if (Take('0')) {
    } else {
      if (position_ >= input_.size() || input_[position_] < '1' ||
          input_[position_] > '9') {
        Fail("数値が不正です");
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    if (Take('.')) {
      if (position_ >= input_.size() || input_[position_] < '0' ||
          input_[position_] > '9') {
        Fail("小数部が不正です");
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      if (position_ >= input_.size() || input_[position_] < '0' ||
          input_[position_] > '9') {
        Fail("指数部が不正です");
      }
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    }
    const std::string token = input_.substr(start, position_ - start);
    char *end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || !std::isfinite(value)) {
      Fail("数値が不正です");
    }
    return Json(value);
  }

  void ConsumeLiteral(const char *literal) {
    while (*literal != '\0') {
      if (position_ >= input_.size() || input_[position_] != *literal) {
        Fail("リテラルが不正です");
      }
      ++position_;
      ++literal;
    }
  }

  void SkipSpace() {
    while (position_ < input_.size()) {
      const char ch = input_[position_];
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
        break;
      ++position_;
    }
  }

  char Peek() const noexcept {
    return position_ < input_.size() ? input_[position_] : '\0';
  }

  bool Take(char expected) {
    if (Peek() != expected)
      return false;
    ++position_;
    return true;
  }

  void Require(char expected) {
    if (!Take(expected)) {
      Fail(std::string("'") + expected + "' が必要です");
    }
  }

  [[noreturn]] void Fail(const std::string &message) const {
    throw std::runtime_error(message + " (位置 " + std::to_string(position_) +
                             ")");
  }

  const std::string &input_;
  std::size_t position_ = 0;
};

void AppendEscaped(std::ostringstream &output, const std::string &value) {
  output << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (ch < 0x20U) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned>(ch) << std::dec;
      } else {
        output << static_cast<char>(ch);
      }
    }
  }
  output << '"';
}

void StringifyValue(std::ostringstream &output, const Json &value, int indent,
                    int depth) {
  const auto writeIndent = [&output, indent](int level) {
    if (indent > 0)
      output << std::string(static_cast<std::size_t>(level * indent), ' ');
  };
  if (value.IsNull()) {
    output << "null";
  } else if (const auto boolean = value.AsBool()) {
    output << (*boolean ? "true" : "false");
  } else if (const auto number = value.AsNumber()) {
    output << std::setprecision(15) << *number;
  } else if (const auto string = value.AsString()) {
    AppendEscaped(output, *string);
  } else if (const auto array = value.AsArray()) {
    output << '[';
    for (std::size_t i = 0; i < array->size(); ++i) {
      if (i != 0)
        output << ',';
      if (indent > 0)
        output << '\n';
      writeIndent(depth + 1);
      StringifyValue(output, (*array)[i], indent, depth + 1);
    }
    if (!array->empty() && indent > 0) {
      output << '\n';
      writeIndent(depth);
    }
    output << ']';
  } else if (const auto object = value.AsObject()) {
    output << '{';
    std::size_t index = 0;
    for (const auto &[key, child] : *object) {
      if (index++ != 0)
        output << ',';
      if (indent > 0)
        output << '\n';
      writeIndent(depth + 1);
      AppendEscaped(output, key);
      output << (indent > 0 ? ": " : ":");
      StringifyValue(output, child, indent, depth + 1);
    }
    if (!object->empty() && indent > 0) {
      output << '\n';
      writeIndent(depth);
    }
    output << '}';
  }
}

} // namespace

std::optional<Json> Json::Parse(const std::string &text, std::string *error) {
  return Parser(text).Parse(error);
}

std::string Json::Stringify(int indent) const {
  std::ostringstream output;
  StringifyValue(output, *this, indent, 0);
  return output.str();
}

bool Json::IsNull() const noexcept {
  return std::holds_alternative<std::nullptr_t>(value_);
}
bool Json::IsObject() const noexcept {
  return std::holds_alternative<Object>(value_);
}
bool Json::IsArray() const noexcept {
  return std::holds_alternative<Array>(value_);
}
bool Json::IsString() const noexcept {
  return std::holds_alternative<std::string>(value_);
}
bool Json::IsNumber() const noexcept {
  return std::holds_alternative<double>(value_);
}
bool Json::IsBool() const noexcept {
  return std::holds_alternative<bool>(value_);
}

const Json::Object *Json::AsObject() const noexcept {
  return std::get_if<Object>(&value_);
}
const Json::Array *Json::AsArray() const noexcept {
  return std::get_if<Array>(&value_);
}
const std::string *Json::AsString() const noexcept {
  return std::get_if<std::string>(&value_);
}

std::optional<double> Json::AsNumber() const noexcept {
  if (const auto value = std::get_if<double>(&value_))
    return *value;
  return std::nullopt;
}

std::optional<bool> Json::AsBool() const noexcept {
  if (const auto value = std::get_if<bool>(&value_))
    return *value;
  return std::nullopt;
}

const Json *Json::Find(const std::string &key) const noexcept {
  const Object *object = AsObject();
  if (object == nullptr)
    return nullptr;
  const auto iterator = object->find(key);
  return iterator == object->end() ? nullptr : &iterator->second;
}

Json &Json::operator[](const std::string &key) {
  if (!IsObject())
    value_ = Object{};
  return std::get<Object>(value_)[key];
}

} // namespace sf
