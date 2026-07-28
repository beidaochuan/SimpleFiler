#include "core/ArchivePath.h"
#include "core/CommandQuery.h"
#include "core/Json.h"
#include "core/Settings.h"
#include "core/TerminalCommand.h"

#include <filesystem>
#include <fstream>
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
  settings.bookmarks.push_back(
      {"bookmark-1", "Work", "C:\\Work", "wk", {"project", "開発"}});
  settings.links.push_back({"link-1", sf::LinkType::Application, "Editor",
                            "C:\\Editor.exe", "--safe", "C:\\", "edit",
                            {"text", "文章"}, true});
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
  Check(loaded.settings.bookmarks[0].alias == "wk" &&
            loaded.settings.bookmarks[0].keywords.size() == 2,
        "Bookmark aliases and keywords should round-trip");
  Check(loaded.settings.links.size() == 1 &&
            loaded.settings.links[0].type == sf::LinkType::Application,
        "Links should round-trip");
  Check(loaded.settings.links[0].alias == "edit" &&
            loaded.settings.links[0].keywords.size() == 2,
        "Application aliases and keywords should round-trip");
  Check(loaded.settings.links[0].runAsAdministrator,
        "Application elevation defaults should round-trip");
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
  Check(escaped == LR"(\\server\A^&B\100^%^^done)",
        "CMD metacharacters should be escaped in UNC paths");
  Check(sf::BuildCmdUncParameters(LR"(\\server\A&B)") ==
            LR"(/D /V:OFF /K pushd ^"\\server\A^&B^")",
        "UNC command should caret-quote the complete path");
  const std::wstring specialUnc =
      sf::BuildCmdUncParameters(LR"(\\server\100%\!folder^(x))");
  const std::wstring expectedSpecialUnc =
      LR"(/D /V:OFF /K pushd ^"\\server\100^%\!folder^^^(x^)^")";
  Check(specialUnc == expectedSpecialUnc,
        "UNC command should preserve percent and exclamation characters");
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

void TestSettingsFailuresAndValidation() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("simplefiler-settings-" + sf::MakeStableId());
  std::error_code filesystemError;
  std::filesystem::create_directories(root, filesystemError);

  const auto corruptPath = root / L"日本語-破損.json";
  {
    std::ofstream stream(corruptPath, std::ios::binary);
    stream << R"({"broken":])";
  }
  const sf::SettingsLoadResult corrupt =
      sf::SettingsStore(corruptPath).Load();
  Check(corrupt.usedDefaults && !corrupt.warning.empty(),
        "Corrupt settings should use defaults with a warning");
  Check(!std::filesystem::exists(corruptPath),
        "Corrupt Unicode settings should be moved aside");
  bool foundBroken = false;
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    const std::wstring filename = entry.path().filename().wstring();
    if (filename.starts_with(L"日本語-破損.json.broken-"))
      foundBroken = true;
  }
  Check(foundBroken, "Corrupt settings backup should preserve Unicode path");

  const auto clampedPath = root / "clamped.json";
  {
    std::ofstream stream(clampedPath, std::ios::binary);
    stream << R"({
      "window":{"width":1,"height":2},
      "layout":{"splitRatio":9},
      "panes":[{"sortColumn":99}]
    })";
  }
  const sf::SettingsLoadResult clamped =
      sf::SettingsStore(clampedPath).Load();
  Check(clamped.settings.windowWidth == 760 &&
            clamped.settings.windowHeight == 420,
        "Window dimensions should be clamped to usable minimums");
  Check(clamped.settings.splitRatio == 0.8,
        "Pane split ratio should be clamped");
  Check(clamped.settings.panes[0].sortColumn == 3,
        "Sort column should be clamped");

  const auto preservedPath = root / "preserved.json";
  const auto source = sf::Json::Parse(
      R"({"bookmarks":[{"id":"b1","name":"Work","path":"C:\\Work","futureEntry":"keep"}]})");
  Check(source.has_value(), "Per-entry preservation fixture should parse");
  if (source) {
    sf::AppSettings settings;
    settings.sourceRoot = *source;
    settings.bookmarks.push_back({"b1", "Work", "C:\\Work"});
    std::string error;
    Check(sf::SettingsStore(preservedPath).Save(settings, &error),
          "Per-entry preservation fixture should save");
    const sf::SettingsLoadResult loaded =
        sf::SettingsStore(preservedPath).Load();
    const sf::Json *bookmarks = loaded.settings.sourceRoot.Find("bookmarks");
    const auto array = bookmarks != nullptr ? bookmarks->AsArray() : nullptr;
    Check(array != nullptr && !array->empty() &&
              (*array)[0].Find("futureEntry") != nullptr,
          "Unknown fields inside registered entries should survive");
  }

  std::string saveError;
  Check(!sf::SettingsStore(root / "missing" / "settings.json")
             .Save(sf::AppSettings{}, &saveError) &&
            !saveError.empty(),
        "Saving into a missing unwritable location should fail explicitly");

  std::filesystem::remove_all(root, filesystemError);
}

void TestCommandQueries() {
  {
    const sf::CommandQuery query = sf::ParseCommandQuery(L"ffwork");
    Check(query.mode == sf::CommandMode::Folder && query.query == L"work",
          "ff should parse without a separating space");
  }
  {
    const sf::CommandQuery query = sf::ParseCommandQuery(L"FF 日本語");
    Check(query.mode == sf::CommandMode::Folder && query.query == L"日本語",
          "ff should be case-insensitive and accept Unicode queries");
  }
  {
    const sf::CommandQuery query = sf::ParseCommandQuery(L"aa editor");
    Check(query.mode == sf::CommandMode::Application &&
              query.query == L"editor",
          "aa should parse application queries");
  }
  {
    const sf::CommandQuery query = sf::ParseCommandQuery(L"cmd admin");
    Check(query.mode == sf::CommandMode::Terminal && query.query == L"admin",
          "cmd should parse terminal options");
  }
  Check(sf::ParseCommandQuery(L"cmd.exe").mode == sf::CommandMode::None,
        "cmd must be a complete command prefix");
  Check(sf::ParseCommandQuery(L"folder").mode == sf::CommandMode::None,
        "ordinary text should not parse as a command");

  const auto exact = sf::CommandMatchScore(L"work", L"work");
  const auto prefix = sf::CommandMatchScore(L"work", L"workspace");
  const auto substring = sf::CommandMatchScore(L"work", L"my work folder");
  const auto fuzzy = sf::CommandMatchScore(L"wk", L"workspace");
  Check(exact && prefix && substring && fuzzy &&
            *exact > *prefix && *prefix > *substring &&
            *substring > *fuzzy,
        "command matches should rank exact, prefix, substring and fuzzy");
  Check(sf::CommandMatchScore(L"日本", L"日本語資料").has_value(),
        "command matching should support Unicode text");
  Check(!sf::CommandMatchScore(L"xyz", L"workspace").has_value(),
        "unmatched command candidates should be rejected");
}

} // namespace

int main() {
  TestJsonRoundTrip();
  TestSettingsRoundTrip();
  TestSettingsFailuresAndValidation();
  TestTerminalCommands();
  TestArchivePaths();
  TestCommandQueries();
  if (failures == 0) {
    std::cout << "All core tests passed\n";
  }
  return failures == 0 ? 0 : 1;
}
