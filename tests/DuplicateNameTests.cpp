#include "core/DuplicateName.h"

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestBuildDuplicateName() {
  Check(sf::BuildDuplicateName(L"photo.jpg", false, 1) == L"photo - コピー.jpg",
        "The first duplicate of a file should append \" - コピー\" before the extension");
  Check(sf::BuildDuplicateName(L"photo.jpg", false, 2) ==
            L"photo - コピー (2).jpg",
        "Later duplicates should append a numbered suffix");
  Check(sf::BuildDuplicateName(L"folder", true, 1) == L"folder - コピー",
        "Directory duplicates should not be split at a dot");
  Check(sf::BuildDuplicateName(L".gitignore", false, 1) ==
            L".gitignore - コピー",
        "A leading dot should not be treated as an extension separator");
}

void TestGenerateDuplicateName() {
  int calls = 0;
  const std::wstring result = sf::GenerateDuplicateName(
      L"photo.jpg", false, [&](const std::wstring &candidate) {
        ++calls;
        return candidate == L"photo - コピー.jpg" ||
               candidate == L"photo - コピー (2).jpg";
      });
  Check(result == L"photo - コピー (3).jpg",
        "GenerateDuplicateName should skip every name reported as existing");
  Check(calls == 3, "GenerateDuplicateName should stop probing once a free name is found");

  const std::wstring fallback = sf::GenerateDuplicateName(
      L"photo.jpg", false, [](const std::wstring &) { return true; });
  Check(!fallback.empty(),
        "GenerateDuplicateName must return a fallback name instead of looping forever");
}

} // namespace

int main() {
  TestBuildDuplicateName();
  TestGenerateDuplicateName();

  if (failures == 0)
    std::cout << "DuplicateName tests passed\n";
  return failures == 0 ? 0 : 1;
}
