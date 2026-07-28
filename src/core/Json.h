#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sf {

class Json final {
public:
  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;
  using Value =
      std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

  Json() noexcept : value_(nullptr) {}
  Json(std::nullptr_t) noexcept : value_(nullptr) {}
  Json(bool value) : value_(value) {}
  Json(int value) : value_(static_cast<double>(value)) {}
  Json(double value) : value_(value) {}
  Json(std::string value) : value_(std::move(value)) {}
  Json(const char *value) : value_(std::string(value)) {}
  Json(Array value) : value_(std::move(value)) {}
  Json(Object value) : value_(std::move(value)) {}

  [[nodiscard]] static std::optional<Json> Parse(const std::string &text,
                                                 std::string *error = nullptr);
  [[nodiscard]] std::string Stringify(int indent = 2) const;

  [[nodiscard]] bool IsNull() const noexcept;
  [[nodiscard]] bool IsObject() const noexcept;
  [[nodiscard]] bool IsArray() const noexcept;
  [[nodiscard]] bool IsString() const noexcept;
  [[nodiscard]] bool IsNumber() const noexcept;
  [[nodiscard]] bool IsBool() const noexcept;

  [[nodiscard]] const Object *AsObject() const noexcept;
  [[nodiscard]] const Array *AsArray() const noexcept;
  [[nodiscard]] const std::string *AsString() const noexcept;
  [[nodiscard]] std::optional<double> AsNumber() const noexcept;
  [[nodiscard]] std::optional<bool> AsBool() const noexcept;

  [[nodiscard]] const Json *Find(const std::string &key) const noexcept;
  Json &operator[](const std::string &key);

private:
  Value value_;
};

} // namespace sf
