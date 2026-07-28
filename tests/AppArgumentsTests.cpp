#include "core/AppArguments.h"

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestWindowsQuoting() {
  Check(sf::QuoteWindowsCommandLineArgument(L"plain.txt") == L"plain.txt",
        "Plain arguments should not need quotes");
  Check(sf::QuoteWindowsCommandLineArgument(L"") == L"\"\"",
        "Empty arguments should be quoted");
  Check(sf::QuoteWindowsCommandLineArgument(LR"(C:\Program Files\app.exe)") ==
            LR"("C:\Program Files\app.exe")",
        "Spaces should cause quoting");
  Check(sf::QuoteWindowsCommandLineArgument(LR"(say "hello")") ==
            LR"("say \"hello\"")",
        "Embedded quotes should be escaped");
  Check(sf::QuoteWindowsCommandLineArgument(L"C:\\Folder With Space\\") ==
            L"\"C:\\Folder With Space\\\\\"",
        "Trailing backslashes should be doubled before the closing quote");
}

void TestExpansion() {
  sf::AppArgumentContext context;
  context.files = {LR"(C:\Work\first file.txt)", LR"(D:\日本語\二番.txt)"};
  context.folder = LR"(C:\Work)";
  context.left = LR"(C:\Left Side)";
  context.right = LR"(D:\Right)";
  context.other = LR"(D:\Right)";

  const auto expanded = sf::ExpandAppArgumentTemplate(
      L"--file {file} --all {files} --folder {folder} --left {left} "
      L"--right {right} --other {other}",
      context);
  Check(static_cast<bool>(expanded), "All documented placeholders should expand");
  Check(expanded.commandLine ==
            LR"(--file "C:\Work\first file.txt" --all "C:\Work\first file.txt" D:\日本語\二番.txt --folder C:\Work --left "C:\Left Side" --right D:\Right --other D:\Right)",
        "Placeholders should expand to independently quoted arguments");

  const auto literalBraces =
      sf::ExpandAppArgumentTemplate(L"--object {{name}} {folder}", context);
  Check(literalBraces &&
            literalBraces.commandLine == LR"(--object {name} C:\Work)",
        "Double braces should produce literal braces");
}

void TestExpansionFailures() {
  sf::AppArgumentContext context;
  context.folder = LR"(C:\Work)";

  const auto missingFile =
      sf::ExpandAppArgumentTemplate(L"--open {file}", context);
  Check(!missingFile && missingFile.commandLine.empty() &&
            !missingFile.error.empty(),
        "A file placeholder without a selected file should fail safely");

  const auto unknown =
      sf::ExpandAppArgumentTemplate(L"--value {typo}", context);
  Check(!unknown && unknown.commandLine.empty() && !unknown.error.empty(),
        "Unknown placeholders should fail safely");

  const auto unclosed =
      sf::ExpandAppArgumentTemplate(L"--value {folder", context);
  Check(!unclosed && unclosed.commandLine.empty() && !unclosed.error.empty(),
        "Unclosed placeholders should fail safely");
}

} // namespace

int main() {
  TestWindowsQuoting();
  TestExpansion();
  TestExpansionFailures();
  if (failures == 0)
    std::cout << "AppArguments tests passed\n";
  return failures == 0 ? 0 : 1;
}
