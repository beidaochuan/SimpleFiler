#pragma once

#include "core/Json.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sf {

enum class LinkType { File, Application };

struct Bookmark final {
  std::string id;
  std::string name;
  std::string path;
  // These fields stay at the end so existing three-value aggregate
  // initializers remain source-compatible.
  std::string alias;
  std::vector<std::string> keywords;
};

struct RegisteredLink final {
  std::string id;
  LinkType type = LinkType::File;
  std::string name;
  std::string target;
  std::string arguments;
  std::string workingDirectory;
  // These fields stay at the end so existing six-value aggregate
  // initializers remain source-compatible.
  std::string alias;
  std::vector<std::string> keywords;
  bool runAsAdministrator = false;
};

struct PaneSettings final {
  std::string path;
  int sortColumn = 0;
  bool sortAscending = true;
  bool showHidden = false;
};

struct AppSettings final {
  static constexpr int kSchemaVersion = 1;

  int windowX = -1;
  int windowY = -1;
  int windowWidth = 1200;
  int windowHeight = 760;
  bool maximized = false;
  bool twoPanes = true;
  double splitRatio = 0.5;
  bool sidebarVisible = true;
  PaneSettings panes[2];
  std::vector<Bookmark> bookmarks;
  std::vector<RegisteredLink> links;

  // Loading starts from the original root so future unknown fields survive a
  // save.
  Json sourceRoot = Json::Object{};
};

struct SettingsLoadResult final {
  AppSettings settings;
  bool usedDefaults = false;
  std::string warning;
};

class SettingsStore final {
public:
  explicit SettingsStore(std::filesystem::path path);

  [[nodiscard]] SettingsLoadResult Load() const;
  [[nodiscard]] bool Save(const AppSettings &settings,
                          std::string *error = nullptr) const;
  [[nodiscard]] const std::filesystem::path &Path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string MakeStableId();

} // namespace sf
