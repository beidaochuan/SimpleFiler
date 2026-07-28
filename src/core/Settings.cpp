#include "core/Settings.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace sf {
namespace {

std::string ReadText(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return {};
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

std::string StringOr(const Json *value, const std::string &fallback = {}) {
  if (value != nullptr) {
    if (const auto string = value->AsString())
      return *string;
  }
  return fallback;
}

int IntOr(const Json *value, int fallback) {
  if (value != nullptr) {
    if (const auto number = value->AsNumber())
      return static_cast<int>(*number);
  }
  return fallback;
}

double NumberOr(const Json *value, double fallback) {
  if (value != nullptr) {
    if (const auto number = value->AsNumber())
      return *number;
  }
  return fallback;
}

bool BoolOr(const Json *value, bool fallback) {
  if (value != nullptr) {
    if (const auto boolean = value->AsBool())
      return *boolean;
  }
  return fallback;
}

Json PaneToJson(const PaneSettings &pane) {
  Json::Object object;
  object["path"] = pane.path;
  object["sortColumn"] = pane.sortColumn;
  object["sortAscending"] = pane.sortAscending;
  object["showHidden"] = pane.showHidden;
  return object;
}

PaneSettings PaneFromJson(const Json &value, PaneSettings fallback) {
  fallback.path = StringOr(value.Find("path"), fallback.path);
  fallback.sortColumn =
      std::clamp(IntOr(value.Find("sortColumn"), fallback.sortColumn), 0, 3);
  fallback.sortAscending =
      BoolOr(value.Find("sortAscending"), fallback.sortAscending);
  fallback.showHidden = BoolOr(value.Find("showHidden"), fallback.showHidden);
  return fallback;
}

std::filesystem::path BrokenPath(const std::filesystem::path &original) {
  const auto count = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  return std::filesystem::path(original.string() + ".broken-" +
                               std::to_string(count));
}

} // namespace

SettingsStore::SettingsStore(std::filesystem::path path)
    : path_(std::move(path)) {}

SettingsLoadResult SettingsStore::Load() const {
  SettingsLoadResult result;
  std::error_code error;
  if (!std::filesystem::exists(path_, error)) {
    result.usedDefaults = true;
    return result;
  }

  const std::string text = ReadText(path_);
  std::string parseError;
  const std::optional<Json> root = Json::Parse(text, &parseError);
  if (!root || !root->IsObject()) {
    result.usedDefaults = true;
    result.warning =
        "設定ファイルが破損しているため既定値で起動しました: " + parseError;
    std::filesystem::rename(path_, BrokenPath(path_), error);
    return result;
  }

  result.settings.sourceRoot = *root;
  const int schema = IntOr(root->Find("schemaVersion"), 0);
  if (schema > AppSettings::kSchemaVersion) {
    result.warning =
        "新しいバージョンの設定です。未知の項目を保持して読み込みました。";
  }

  if (const Json *window = root->Find("window")) {
    result.settings.windowX = IntOr(window->Find("x"), result.settings.windowX);
    result.settings.windowY = IntOr(window->Find("y"), result.settings.windowY);
    result.settings.windowWidth = std::max(
        760, IntOr(window->Find("width"), result.settings.windowWidth));
    result.settings.windowHeight = std::max(
        420, IntOr(window->Find("height"), result.settings.windowHeight));
    result.settings.maximized = BoolOr(window->Find("maximized"), false);
  }
  if (const Json *layout = root->Find("layout")) {
    result.settings.twoPanes = BoolOr(layout->Find("twoPanes"), true);
    result.settings.splitRatio =
        std::clamp(NumberOr(layout->Find("splitRatio"), 0.5), 0.2, 0.8);
    result.settings.sidebarVisible =
        BoolOr(layout->Find("sidebarVisible"), true);
  }
  if (const Json *panes = root->Find("panes"); panes != nullptr) {
    if (const auto array = panes->AsArray()) {
      for (std::size_t index = 0;
           index < std::min<std::size_t>(2, array->size()); ++index) {
        result.settings.panes[index] =
            PaneFromJson((*array)[index], result.settings.panes[index]);
      }
    }
  }
  if (const Json *bookmarks = root->Find("bookmarks"); bookmarks != nullptr) {
    if (const auto array = bookmarks->AsArray()) {
      for (const Json &value : *array) {
        Bookmark bookmark;
        bookmark.id = StringOr(value.Find("id"));
        bookmark.name = StringOr(value.Find("name"));
        bookmark.path = StringOr(value.Find("path"));
        if (!bookmark.name.empty() && !bookmark.path.empty()) {
          if (bookmark.id.empty())
            bookmark.id = MakeStableId();
          result.settings.bookmarks.push_back(std::move(bookmark));
        }
      }
    }
  }
  if (const Json *links = root->Find("links"); links != nullptr) {
    if (const auto array = links->AsArray()) {
      for (const Json &value : *array) {
        RegisteredLink link;
        link.id = StringOr(value.Find("id"));
        link.type = StringOr(value.Find("type")) == "app"
                        ? LinkType::Application
                        : LinkType::File;
        link.name = StringOr(value.Find("name"));
        link.target = StringOr(value.Find("target"));
        link.arguments = StringOr(value.Find("arguments"));
        link.workingDirectory = StringOr(value.Find("workingDirectory"));
        if (!link.name.empty() && !link.target.empty()) {
          if (link.id.empty())
            link.id = MakeStableId();
          result.settings.links.push_back(std::move(link));
        }
      }
    }
  }
  return result;
}

bool SettingsStore::Save(const AppSettings &settings,
                         std::string *errorMessage) const {
  Json root =
      settings.sourceRoot.IsObject() ? settings.sourceRoot : Json::Object{};
  root["schemaVersion"] = AppSettings::kSchemaVersion;

  Json::Object window;
  window["x"] = settings.windowX;
  window["y"] = settings.windowY;
  window["width"] = settings.windowWidth;
  window["height"] = settings.windowHeight;
  window["maximized"] = settings.maximized;
  root["window"] = std::move(window);

  Json::Object layout;
  layout["twoPanes"] = settings.twoPanes;
  layout["splitRatio"] = settings.splitRatio;
  layout["sidebarVisible"] = settings.sidebarVisible;
  root["layout"] = std::move(layout);

  Json::Array panes;
  panes.push_back(PaneToJson(settings.panes[0]));
  panes.push_back(PaneToJson(settings.panes[1]));
  root["panes"] = std::move(panes);

  Json::Array bookmarks;
  for (const Bookmark &bookmark : settings.bookmarks) {
    Json::Object value;
    value["id"] = bookmark.id;
    value["name"] = bookmark.name;
    value["path"] = bookmark.path;
    bookmarks.emplace_back(std::move(value));
  }
  root["bookmarks"] = std::move(bookmarks);

  Json::Array links;
  for (const RegisteredLink &link : settings.links) {
    Json::Object value;
    value["id"] = link.id;
    value["type"] = link.type == LinkType::Application ? "app" : "file";
    value["name"] = link.name;
    value["target"] = link.target;
    value["arguments"] = link.arguments;
    value["workingDirectory"] = link.workingDirectory;
    links.emplace_back(std::move(value));
  }
  root["links"] = std::move(links);

  const std::filesystem::path temporary = path_.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      if (errorMessage)
        *errorMessage = "一時設定ファイルを作成できません";
      return false;
    }
    stream << root.Stringify(2) << '\n';
    stream.flush();
    if (!stream) {
      if (errorMessage)
        *errorMessage = "設定ファイルを書き込めません";
      return false;
    }
  }

#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    if (errorMessage)
      *errorMessage = "設定ファイルを置き換えられません";
    DeleteFileW(temporary.c_str());
    return false;
  }
#else
  std::error_code filesystemError;
  std::filesystem::rename(temporary, path_, filesystemError);
  if (filesystemError) {
    std::filesystem::remove(path_, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, path_, filesystemError);
  }
  if (filesystemError) {
    if (errorMessage)
      *errorMessage = filesystemError.message();
    return false;
  }
#endif
  return true;
}

std::string MakeStableId() {
  static std::mt19937_64 generator(std::random_device{}());
  std::ostringstream output;
  output << std::hex << generator() << generator();
  return output.str();
}

} // namespace sf
