#include "core/ArchivePath.h"
#include "core/Json.h"
#include "core/Settings.h"
#include "core/TerminalCommand.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestJsonRoundTrip() {
  const std::string input =
      R"({"name":"SimpleFiler","unicode":"\u65e5\u672c\u8a9e","items":[1,true,null]})";
  std::string error;
  const auto json = sf::Json::Parse(input, &error);
  Check(json.has_value(), "JSON should parse");
  if (!json)
    return;
  Check(json->Find("name") != nullptr &&
            *json->Find("name")->AsString() == "SimpleFiler",
        "JSON string should be available");
  const auto reparsed = sf::Json::Parse(json->Stringify());
  Check(reparsed.has_value(), "Stringified JSON should parse");
  Check(reparsed && reparsed->Find("unicode") != nullptr &&
            *reparsed->Find("unicode")->AsString() == "日本語",
        "Unicode escapes should decode to UTF-8");
  Check(!sf::Json::Parse(R"({"broken":])").has_value(),
        "Invalid JSON should fail");
  Check(!sf::Json::Parse(R"("\uDC00")").has_value(),
        "Unpaired low surrogate should fail");
}

void TestSettingsRoundTrip() {
  const auto path = std::filesystem::temp_directory_path() /
                    ("simplefiler-test-" + sf::MakeStableId() + ".json");
  sf::SettingsStore store(path);
  sf::AppSettings settings;
  settings.twoPanes = false;
  settings.splitRatio = 0.65;
  settings.panes[0].path = "C:\\日本語";
  settings.bookmarks.push_back({"bookmark-1", "Work", "C:\\Work"});
  settings.links.push_back({"link-1", sf::LinkType::Application, "Editor",
                            "C:\\Editor.exe", "--safe", "C:\\"});
  settings.sourceRoot["futureSetting"] = "preserve-me";

  std::string error;
  Check(store.Save(settings, &error), "Settings should save");
  const sf::SettingsLoadResult loaded = store.Load();
  Check(!loaded.usedDefaults, "Saved settings should load");
  Check(!loaded.settings.twoPanes, "Layout should round-trip");
  Check(loaded.settings.splitRatio == 0.65, "Split ratio should round-trip");
  Check(loaded.settings.panes[0].path == "C:\\日本語",
        "UTF-8 path should round-trip");
  Check(loaded.settings.bookmarks.size() == 1, "Bookmarks should round-trip");
  Check(loaded.settings.links.size() == 1 &&
            loaded.settings.links[0].type == sf::LinkType::Application,
        "Links should round-trip");
  Check(loaded.settings.sourceRoot.Find("futureSetting") != nullptr,
        "Unknown settings should be preserved");
  sf::AppSettings changed = loaded.settings;
  changed.sidebarVisible = false;
  Check(store.Save(changed, &error), "Loaded settings should save again");
  const sf::SettingsLoadResult loadedAgain = store.Load();
  Check(loadedAgain.settings.sourceRoot.Find("futureSetting") != nullptr,
        "Unknown settings should survive subsequent saves");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void TestTerminalCommands() {
  Check(sf::Base64Encode("Man") == "TWFu",
        "Base64 should encode complete blocks");
  Check(sf::Base64Encode("Ma") == "TWE=",
        "Base64 should add one padding character");
  Check(sf::Base64Encode("M") == "TQ==",
        "Base64 should add two padding characters");
  const std::string command =
      sf::Utf8ToPowerShellEncodedCommand("C:\\日本語 & 100%");
  Check(!command.empty() && command.find_first_not_of(
                                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                                "uvwxyz0123456789+/=") == std::string::npos,
        "PowerShell command should be Base64");
  const std::wstring escaped =
      sf::EscapeCmdUncPath(LR"(\\server\A&B\100%^done)");
  Check(escaped == LR"(\\server\A&B\100%^done)",
        "Quoted CMD paths should preserve valid file-name characters");
  Check(sf::BuildCmdUncParameters(LR"(\\server\A&B)") ==
            LR"(/D /S /K "pushd "\\server\A&B"")",
        "UNC command should keep the path inside an inner quote pair");
}

void TestArchivePaths() {
  Check(sf::IsSafeArchivePath("folder/file.txt"),
        "Normal relative archive path should be accepted");
  Check(sf::IsSafeArchivePath("日本語/空/"),
        "UTF-8 directory entry should be accepted");
  Check(!sf::IsSafeArchivePath("../outside.txt"),
        "Parent traversal should be rejected");
  Check(!sf::IsSafeArchivePath("/absolute.txt"),
        "Absolute archive path should be rejected");
  Check(!sf::IsSafeArchivePath("C:/absolute.txt"),
        "Drive-qualified archive path should be rejected");
  Check(!sf::IsSafeArchivePath("folder/file.txt:stream"),
        "Alternate data streams should be rejected");
  Check(!sf::IsSafeArchivePath("folder/NUL.txt"),
        "Reserved Windows device names should be rejected");
}

} // namespace

int main() {
  TestJsonRoundTrip();
  TestSettingsRoundTrip();
  TestTerminalCommands();
  TestArchivePaths();
  if (failures == 0) {
    std::cout << "All core tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
