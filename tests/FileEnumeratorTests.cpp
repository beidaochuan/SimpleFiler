#include "win/AppMessages.h"
#include "win/FileEnumerator.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

struct MessageCapture final {
  std::vector<sf::win::FileItem> items;
  std::vector<std::size_t> batchSizes;
  std::optional<sf::win::EnumerationDone> done;
  bool receivedBatchAfterDone = false;

  void Reset() {
    items.clear();
    batchSizes.clear();
    done.reset();
    receivedBatchAfterDone = false;
  }
};

MessageCapture *activeCapture = nullptr;

LRESULT CALLBACK TestWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  if (message == sf::win::kMessageEnumerationBatch) {
    std::unique_ptr<sf::win::EnumerationBatch> batch(
        reinterpret_cast<sf::win::EnumerationBatch *>(lParam));
    if (activeCapture != nullptr) {
      activeCapture->receivedBatchAfterDone =
          activeCapture->receivedBatchAfterDone ||
          activeCapture->done.has_value();
      activeCapture->batchSizes.push_back(batch->items.size());
      activeCapture->items.insert(
          activeCapture->items.end(),
          std::make_move_iterator(batch->items.begin()),
          std::make_move_iterator(batch->items.end()));
    }
    return 0;
  }
  if (message == sf::win::kMessageEnumerationDone) {
    std::unique_ptr<sf::win::EnumerationDone> done(
        reinterpret_cast<sf::win::EnumerationDone *>(lParam));
    if (activeCapture != nullptr)
      activeCapture->done = *done;
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

void DrainMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

bool WaitForDone(MessageCapture &capture,
                 std::chrono::steady_clock::duration timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!capture.done && std::chrono::steady_clock::now() < deadline) {
    DrainMessages();
    if (!capture.done)
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
  }
  DrainMessages();
  return capture.done.has_value();
}

template <typename Work>
bool RunWorkerAndWait(MessageCapture &capture, Work work,
                      std::chrono::steady_clock::duration timeout = 20s) {
  capture.Reset();
  activeCapture = &capture;
  std::jthread worker(
      [work = std::move(work)](std::stop_token token) mutable { work(token); });
  const bool completed = WaitForDone(capture, timeout);
  if (!completed)
    worker.request_stop();
  worker.join();
  if (!completed)
    WaitForDone(capture, 2s);
  DrainMessages();
  activeCapture = nullptr;
  return completed;
}

class TemporaryTree final {
public:
  TemporaryTree() {
    const auto temp = std::filesystem::temp_directory_path();
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
      const auto name =
          std::format(L"simplefiler-enumerator-{}-{}-{}", GetCurrentProcessId(),
                      GetTickCount64(), attempt);
      root_ = temp / name;
      std::error_code error;
      if (std::filesystem::create_directory(root_, error))
        return;
    }
    root_.clear();
  }

  TemporaryTree(const TemporaryTree &) = delete;
  TemporaryTree &operator=(const TemporaryTree &) = delete;

  ~TemporaryTree() {
    if (root_.empty())
      return;
    std::error_code error;
    const auto normalizedRoot = std::filesystem::weakly_canonical(root_, error);
    if (error)
      return;
    const auto normalizedTemp = std::filesystem::weakly_canonical(
        std::filesystem::temp_directory_path(), error);
    if (error || normalizedRoot.parent_path() != normalizedTemp ||
        normalizedRoot.filename().native().rfind(
            L"simplefiler-enumerator-", 0) != 0) {
      return;
    }
    std::filesystem::remove_all(normalizedRoot, error);
  }

  [[nodiscard]] const std::filesystem::path &Root() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

bool CreateEmptyFile(const std::filesystem::path &path) {
  const HANDLE file =
      CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  return CloseHandle(file) != FALSE;
}

bool HasName(const MessageCapture &capture, const std::wstring &name) {
  return std::ranges::any_of(capture.items, [&name](const auto &item) {
    return item.name == name;
  });
}

std::size_t CountName(const MessageCapture &capture, const std::wstring &name) {
  return static_cast<std::size_t>(
      std::ranges::count(capture.items, name, &sf::win::FileItem::name));
}

bool HasUniquePaths(const MessageCapture &capture) {
  std::unordered_set<std::wstring> paths;
  for (const auto &item : capture.items) {
    if (!paths.insert(item.path).second)
      return false;
  }
  return true;
}

} // namespace

int main() {
  int failures = 0;
  int skipped = 0;
  const auto check = [&failures](bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };
  const auto skip = [&skipped](const char *message) {
    std::cout << "SKIP: " << message << '\n';
    ++skipped;
  };

  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = TestWindowProcedure;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = L"SimpleFiler.FileEnumeratorTestWindow";
  if (RegisterClassW(&windowClass) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::cerr << "Test window class registration failed\n";
    return 1;
  }
  const HWND window =
      CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                      HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
  if (window == nullptr) {
    std::cerr << "Test message window creation failed\n";
    return 1;
  }

  TemporaryTree temporary;
  check(!temporary.Root().empty(), "Temporary test directory should be made");
  if (temporary.Root().empty()) {
    DestroyWindow(window);
    return 1;
  }

  const auto root = temporary.Root() / L"files";
  const auto child = root / L"child";
  const auto hiddenDirectory = root / L"hidden-folder";
  std::error_code filesystemError;
  std::filesystem::create_directories(child, filesystemError);
  check(!filesystemError, "Nested test directory should be made");
  filesystemError.clear();
  std::filesystem::create_directories(hiddenDirectory, filesystemError);
  check(!filesystemError, "Hidden test directory should be made");

  const std::wstring unicodeName =
      L"\u65e5\u672c\u8a9e-\u30d5\u30a1\u30a4\u30eb.txt";
  check(CreateEmptyFile(root / L"visible.txt"),
        "Visible test file should be made");
  check(CreateEmptyFile(root / L"hidden-item.txt"),
        "Hidden test file should be made");
  check(CreateEmptyFile(root / unicodeName),
        "Unicode-name test file should be made");
  check(CreateEmptyFile(child / L"MixedCaseNeedle.TXT"),
        "Recursive search test file should be made");
  check(CreateEmptyFile(hiddenDirectory / L"nested-hidden-needle.txt"),
        "File inside hidden directory should be made");
  check(SetFileAttributesW((root / L"hidden-item.txt").c_str(),
                           FILE_ATTRIBUTE_HIDDEN) != FALSE,
        "Hidden attribute should be set on test file");
  check(SetFileAttributesW(hiddenDirectory.c_str(),
                           FILE_ATTRIBUTE_DIRECTORY |
                               FILE_ATTRIBUTE_HIDDEN) != FALSE,
        "Hidden attribute should be set on test directory");

  MessageCapture capture;
  constexpr int pane = 1;
  constexpr std::uint64_t generation = 42;

  check(RunWorkerAndWait(capture, [&](std::stop_token token) {
          sf::win::EnumerateDirectory(window, pane, generation, root.wstring(),
                                      false, token);
        }),
        "Visible-only enumeration should finish");
  check(capture.done && capture.done->error == ERROR_SUCCESS,
        "Visible-only enumeration should succeed");
  check(capture.done && capture.done->pane == pane &&
            capture.done->generation == generation,
        "Enumeration completion should preserve pane and generation");
  check(HasName(capture, L"visible.txt"),
        "Visible-only enumeration should include visible files");
  check(HasName(capture, unicodeName),
        "Enumeration should preserve Unicode file names");
  check(!HasName(capture, L"hidden-item.txt"),
        "Visible-only enumeration should omit hidden files");
  check(!HasName(capture, L"hidden-folder"),
        "Visible-only enumeration should omit hidden directories");
  check(capture.done && capture.done->itemCount == capture.items.size(),
        "Enumeration item count should match delivered items");
  check(HasUniquePaths(capture),
        "Enumeration should not deliver duplicate item paths");

  check(RunWorkerAndWait(capture, [&](std::stop_token token) {
          sf::win::EnumerateDirectory(window, pane, generation + 1,
                                      root.wstring(), true, token);
        }),
        "Hidden-inclusive enumeration should finish");
  check(capture.done && capture.done->error == ERROR_SUCCESS,
        "Hidden-inclusive enumeration should succeed");
  check(HasName(capture, L"hidden-item.txt"),
        "Hidden-inclusive enumeration should include hidden files");
  check(HasName(capture, L"hidden-folder"),
        "Hidden-inclusive enumeration should include hidden directories");

  check(RunWorkerAndWait(capture, [&](std::stop_token token) {
          sf::win::SearchDirectory(window, pane, generation + 2, root.wstring(),
                                   L"needle", false, token);
        }),
        "Recursive case-insensitive search should finish");
  check(capture.done && capture.done->error == ERROR_SUCCESS,
        "Recursive case-insensitive search should succeed");
  check(HasName(capture, L"MixedCaseNeedle.TXT"),
        "Search should match case-insensitively in child directories");
  check(!HasName(capture, L"nested-hidden-needle.txt"),
        "Search should not recurse into hidden directories when hidden items "
        "are disabled");
  check(capture.done && capture.done->itemCount == capture.items.size(),
        "Search item count should match delivered items");

  check(RunWorkerAndWait(capture, [&](std::stop_token token) {
          sf::win::SearchDirectory(window, pane, generation + 3, root.wstring(),
                                   L"NEEDLE", true, token);
        }),
        "Hidden-inclusive recursive search should finish");
  check(HasName(capture, L"MixedCaseNeedle.TXT"),
        "Uppercase query should find mixed-case names");
  check(HasName(capture, L"nested-hidden-needle.txt"),
        "Hidden-inclusive search should recurse into hidden directories");

  const auto outside = temporary.Root() / L"outside";
  std::filesystem::create_directory(outside, filesystemError);
  check(!filesystemError, "Reparse target directory should be made");
  check(CreateEmptyFile(outside / L"reparse-only-needle.txt"),
        "Reparse target file should be made");
  const auto link = root / L"linked-outside";
  constexpr DWORD allowUnprivilegedCreate = 0x2;
  bool linkMade =
      CreateSymbolicLinkW(link.c_str(), outside.c_str(),
                          SYMBOLIC_LINK_FLAG_DIRECTORY |
                              allowUnprivilegedCreate) != FALSE;
  if (!linkMade && GetLastError() == ERROR_INVALID_PARAMETER) {
    linkMade =
        CreateSymbolicLinkW(link.c_str(), outside.c_str(),
                            SYMBOLIC_LINK_FLAG_DIRECTORY) != FALSE;
  }
  if (linkMade) {
    check(RunWorkerAndWait(capture, [&](std::stop_token token) {
            sf::win::SearchDirectory(window, pane, generation + 4,
                                     root.wstring(), L"reparse-only", true,
                                     token);
          }),
          "Search with a directory reparse point should finish");
    check(CountName(capture, L"reparse-only-needle.txt") == 0,
          "Recursive search should not traverse directory reparse points");
    check(std::filesystem::remove(link, filesystemError),
          "Directory symbolic link should be removed without touching target");
    check(std::filesystem::exists(outside / L"reparse-only-needle.txt"),
          "Removing test link should preserve its target");
  } else {
    skip("Directory symbolic links are unavailable; reparse traversal test");
  }

  capture.Reset();
  activeCapture = &capture;
  std::stop_source stopped;
  stopped.request_stop();
  sf::win::SearchDirectory(window, pane, generation + 5, root.wstring(),
                           L"needle", true, stopped.get_token());
  check(WaitForDone(capture, 2s),
        "A pre-cancelled search should still post completion");
  check(capture.done && capture.done->error == ERROR_CANCELLED,
        "A pre-cancelled search should report ERROR_CANCELLED");
  check(capture.done && capture.done->itemCount == 0 && capture.items.empty(),
        "A pre-cancelled search should not deliver results");
  activeCapture = nullptr;

  check(sf::win::FormatFileSize(0) == L"0 B",
        "Zero-byte size should use bytes");
  check(sf::win::FormatFileSize(1023) == L"1023 B",
        "Sub-kilobyte size should use bytes");
  check(sf::win::FormatFileSize(1024) == L"1.0 KB",
        "One kibibyte should format as 1.0 KB");
  check(sf::win::FormatFileSize(1536) == L"1.5 KB",
        "Fractional kibibytes should use one decimal place");
  check(sf::win::FormatFileSize(1024ULL * 1024ULL) == L"1.0 MB",
        "One mebibyte should format as 1.0 MB");
  check(sf::win::FormatFileSize(5ULL * 1024ULL * 1024ULL * 1024ULL) ==
            L"5.0 GB",
        "Gibibyte values should use GB");
  check(sf::win::FormatFileSize(3ULL * 1024ULL * 1024ULL * 1024ULL *
                                    1024ULL) == L"3.0 TB",
        "Tebibyte values should use TB");

  SYSTEMTIME localTime{};
  localTime.wYear = 2020;
  localTime.wMonth = 1;
  localTime.wDay = 15;
  localTime.wHour = 12;
  localTime.wMinute = 34;
  SYSTEMTIME utcTime{};
  FILETIME fileTime{};
  check(TzSpecificLocalTimeToSystemTime(nullptr, &localTime, &utcTime) != FALSE,
        "Local test time should convert to UTC");
  check(SystemTimeToFileTime(&utcTime, &fileTime) != FALSE,
        "UTC test time should convert to FILETIME");
  check(sf::win::FormatFileTime(fileTime) == L"2020-01-15 12:34",
        "File time should format in local time with minute precision");

  const auto largeDirectory = temporary.Root() / L"large";
  std::filesystem::create_directory(largeDirectory, filesystemError);
  check(!filesystemError, "Large test directory should be made");
  constexpr std::size_t largeItemCount = 10'000;
  bool allLargeItemsCreated = true;
  for (std::size_t index = 0; index < largeItemCount; ++index) {
    const auto fileName = std::format(L"item-{:05}.tmp", index);
    if (!CreateEmptyFile(largeDirectory / fileName)) {
      allLargeItemsCreated = false;
      break;
    }
  }
  check(allLargeItemsCreated, "10,000 test files should be made");
  if (allLargeItemsCreated) {
    const auto started = std::chrono::steady_clock::now();
    check(RunWorkerAndWait(
              capture,
              [&](std::stop_token token) {
                sf::win::EnumerateDirectory(window, pane, generation + 6,
                                            largeDirectory.wstring(), false,
                                            token);
              },
              60s),
          "10,000-item enumeration should finish without blocking message "
          "dispatch");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    check(capture.done && capture.done->error == ERROR_SUCCESS,
          "10,000-item enumeration should succeed");
    check(capture.done && capture.done->itemCount == largeItemCount,
          "10,000-item enumeration should report the exact total");
    check(capture.items.size() == largeItemCount,
          "10,000-item enumeration should deliver every item");
    check(capture.batchSizes.size() > 1,
          "Large enumeration should stream more than one batch");
    check(std::ranges::all_of(capture.batchSizes, [](std::size_t count) {
            return count > 0 && count <= 256;
          }),
          "Enumeration batches should remain bounded for UI responsiveness");
    check(!capture.receivedBatchAfterDone,
          "Enumeration completion should follow all result batches");
    check(elapsed < 30s,
          "10,000-item enumeration should complete within 30 seconds");
  }

  activeCapture = nullptr;
  DrainMessages();
  DestroyWindow(window);
  if (failures == 0) {
    std::cout << "File enumerator tests passed";
    if (skipped != 0)
      std::cout << " (" << skipped << " optional test skipped)";
    std::cout << '\n';
  }
  return failures == 0 ? 0 : 1;
}
