#include "core/PortablePath.h"

#include <filesystem>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestEmptyPaths() {
  const std::filesystem::path applicationDirectory =
#ifdef _WIN32
      LR"(C:\Tools\SimpleFiler)";
#else
      "/opt/SimpleFiler";
#endif

  Check(sf::ResolvePortablePath({}, applicationDirectory).empty(),
        "Resolving an empty path should return an empty path");
  Check(sf::MakePortablePath({}, applicationDirectory).empty(),
        "Making an empty path portable should return an empty path");
}

void TestResolvePortablePath() {
  const std::filesystem::path applicationDirectory =
#ifdef _WIN32
      LR"(C:\Tools\SimpleFiler)";
  const std::filesystem::path expectedChild =
      LR"(C:\Tools\SimpleFiler\apps\editor.exe)";
  const std::filesystem::path expectedParent = LR"(C:\Tools\shared.exe)";
  const std::filesystem::path absolute = LR"(D:\External\viewer.exe)";
#else
      "/opt/SimpleFiler";
  const std::filesystem::path expectedChild =
      "/opt/SimpleFiler/apps/editor.exe";
  const std::filesystem::path expectedParent = "/opt/shared";
  const std::filesystem::path absolute = "/usr/bin/viewer";
#endif

  Check(sf::ResolvePortablePath(
            std::filesystem::path("apps") / "." / "tools" / ".." /
                "editor.exe",
            applicationDirectory) == expectedChild,
        "Relative paths should resolve against the application directory");
  Check(sf::ResolvePortablePath(
            std::filesystem::path("..") /
#ifdef _WIN32
                "shared.exe",
#else
                "shared",
#endif
            applicationDirectory) == expectedParent,
        "Parent components should be normalized when resolving");
  Check(sf::ResolvePortablePath(absolute, applicationDirectory) == absolute,
        "Absolute stored paths should remain absolute");
  Check(sf::ResolvePortablePath(L".", applicationDirectory) ==
            applicationDirectory,
        "A dot should resolve to the application directory");
}

void TestMakePortablePath() {
  const std::filesystem::path applicationDirectory =
#ifdef _WIN32
      LR"(C:\Tools\SimpleFiler)";
  const std::filesystem::path child =
      LR"(C:\Tools\SimpleFiler\apps\editor.exe)";
  const std::filesystem::path normalizedChild =
      LR"(C:\Tools\SimpleFiler\apps\editor.exe)";
  const std::filesystem::path sibling =
      LR"(C:\Tools\SimpleFiler2\editor.exe)";
  const std::filesystem::path parent = LR"(C:\Tools\shared.exe)";
#else
      "/opt/SimpleFiler";
  const std::filesystem::path child =
      "/opt/SimpleFiler/apps/editor";
  const std::filesystem::path normalizedChild =
      "/opt/SimpleFiler/apps/editor";
  const std::filesystem::path sibling =
      "/opt/SimpleFiler2/editor";
  const std::filesystem::path parent = "/opt/shared";
#endif

  Check(sf::MakePortablePath(child, applicationDirectory) ==
            std::filesystem::path("apps") /
#ifdef _WIN32
                "editor.exe",
#else
                "editor",
#endif
        "Children should become relative paths");
  Check(sf::MakePortablePath(applicationDirectory, applicationDirectory) ==
            std::filesystem::path(L"."),
        "The application directory itself should become a dot");
  Check(sf::MakePortablePath(sibling, applicationDirectory) == sibling,
        "A sibling with a matching text prefix must remain absolute");
  Check(sf::MakePortablePath(parent, applicationDirectory) == parent,
        "A parent path must remain absolute");

  const std::filesystem::path withDotDot =
#ifdef _WIN32
      LR"(C:\Tools\SimpleFiler\temp\..\apps\editor.exe)";
#else
      "/opt/SimpleFiler/temp/../apps/editor";
#endif
  Check(sf::MakePortablePath(withDotDot, applicationDirectory) ==
            sf::MakePortablePath(normalizedChild, applicationDirectory),
        "Dot-dot components should be normalized before containment checks");

  Check(sf::MakePortablePath(
            std::filesystem::path("apps") / "." /
#ifdef _WIN32
                "editor.exe",
#else
                "editor",
#endif
            applicationDirectory) ==
            std::filesystem::path("apps") /
#ifdef _WIN32
                "editor.exe",
#else
                "editor",
#endif
        "Already-relative paths should remain relative and be normalized");
}

void TestUnicode() {
  const std::filesystem::path applicationDirectory =
#ifdef _WIN32
      LR"(C:\道具\SimpleFiler)";
  const std::filesystem::path child =
      LR"(C:\道具\SimpleFiler\アプリ\画像.exe)";
  const std::filesystem::path relative = LR"(アプリ\画像.exe)";
#else
      L"/opt/道具/SimpleFiler";
  const std::filesystem::path child =
      L"/opt/道具/SimpleFiler/アプリ/画像";
  const std::filesystem::path relative = L"アプリ/画像";
#endif

  Check(sf::MakePortablePath(child, applicationDirectory) == relative,
        "Unicode child paths should become portable");
  Check(sf::ResolvePortablePath(relative, applicationDirectory) == child,
        "Unicode portable paths should resolve");
}

#ifdef _WIN32
void TestWindowsSemantics() {
  const std::filesystem::path applicationDirectory =
      LR"(C:\Tools\SimpleFiler)";
  Check(sf::MakePortablePath(
            LR"(c:\TOOLS\simplefiler\Apps\Editor.exe)",
            applicationDirectory) == LR"(Apps\Editor.exe)",
        "Windows containment should be case-insensitive");
  Check(sf::MakePortablePath(
            LR"(D:\Tools\SimpleFiler\Apps\Editor.exe)",
            applicationDirectory) ==
            LR"(D:\Tools\SimpleFiler\Apps\Editor.exe)",
        "A path on another drive must remain absolute");
  Check(sf::MakePortablePath(
            LR"(\\server\share\SimpleFiler\Apps\Editor.exe)",
            LR"(\\SERVER\SHARE\SimpleFiler)") == LR"(Apps\Editor.exe)",
        "UNC roots and components should compare case-insensitively");
}
#endif

} // namespace

int main() {
  TestEmptyPaths();
  TestResolvePortablePath();
  TestMakePortablePath();
  TestUnicode();
#ifdef _WIN32
  TestWindowsSemantics();
#endif

  if (failures == 0)
    std::cout << "PortablePath tests passed\n";
  return failures == 0 ? 0 : 1;
}
