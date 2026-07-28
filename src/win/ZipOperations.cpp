#include "win/ZipOperations.h"

#include "core/ArchivePath.h"
#include "win/AppMessages.h"
#include "win/WinUtils.h"

#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <filesystem>
#include <thread>

namespace sf::win {
namespace {

DWORD NotificationProcessId(HWND window) {
  DWORD processId = 0;
  GetWindowThreadProcessId(window, &processId);
  return processId;
}

void Notify(HWND window, DWORD expectedProcessId, bool success,
            std::wstring message) {
  auto *result = new ZipResult{success, std::move(message)};
  if (expectedProcessId == 0 ||
      NotificationProcessId(window) != expectedProcessId) {
    delete result;
    return;
  }
  if (!PostMessageW(window, kMessageZipDone, 0,
                    reinterpret_cast<LPARAM>(result))) {
    delete result;
  }
}

std::wstring ZipErrorMessage(int32_t error) {
  return L"ZIP処理に失敗しました（コード " + std::to_wstring(error) + L"）";
}

} // namespace

void CreateZipAsync(HWND notifyWindow, std::vector<std::wstring> sources,
                    std::wstring outputPath) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  std::thread([notifyWindow, notificationProcessId,
               sources = std::move(sources),
               outputPath = std::move(outputPath)] {
    void *writer = mz_zip_writer_create();
    if (writer == nullptr) {
      Notify(notifyWindow, notificationProcessId, false,
             L"ZIPライターを初期化できません");
      return;
    }
    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);
    const std::string output = WideToUtf8(ToExtendedPath(outputPath));
    int32_t result = mz_zip_writer_open_file(writer, output.c_str(), 0, 0);
    for (const std::wstring &source : sources) {
      if (result != MZ_OK)
        break;
      const std::filesystem::path value(source);
      const std::string sourceUtf8 =
          WideToUtf8(ToExtendedPath(value.wstring()));
      std::wstring root = ToExtendedPath(value.parent_path().wstring());
      if (!root.empty() && root.back() != L'\\' && root.back() != L'/') {
        root.push_back(L'\\');
      }
      const std::string rootUtf8 = WideToUtf8(root);
      result = mz_zip_writer_add_path(writer, sourceUtf8.c_str(),
                                      rootUtf8.c_str(), 0, 1);
    }
    const int32_t closeResult = mz_zip_writer_close(writer);
    mz_zip_writer_delete(&writer);
    if (result == MZ_OK && closeResult == MZ_OK) {
      Notify(notifyWindow, notificationProcessId, true,
             L"ZIPを作成しました: " + outputPath);
    } else {
      DeleteFileW(outputPath.c_str());
      Notify(notifyWindow, notificationProcessId, false,
             ZipErrorMessage(result != MZ_OK ? result : closeResult));
    }
  }).detach();
}

void ExtractZipAsync(HWND notifyWindow, std::wstring archivePath,
                     std::wstring destination) {
  const DWORD notificationProcessId = NotificationProcessId(notifyWindow);
  std::thread([notifyWindow, notificationProcessId,
               archivePath = std::move(archivePath),
               destination = std::move(destination)] {
    void *reader = mz_zip_reader_create();
    if (reader == nullptr) {
      Notify(notifyWindow, notificationProcessId, false,
             L"ZIPリーダーを初期化できません");
      return;
    }
    const std::string archive = WideToUtf8(ToExtendedPath(archivePath));
    int32_t result = mz_zip_reader_open_file(reader, archive.c_str());
    if (result == MZ_OK) {
      result = mz_zip_reader_goto_first_entry(reader);
      while (result == MZ_OK) {
        mz_zip_file *information = nullptr;
        result = mz_zip_reader_entry_get_info(reader, &information);
        if (result != MZ_OK)
          break;
        if (information == nullptr ||
            !IsSafeArchivePath(information->filename != nullptr
                                   ? information->filename
                                   : "") ||
            (information->linkname != nullptr &&
             information->linkname[0] != '\0')) {
          result = MZ_FORMAT_ERROR;
          break;
        }
        result = mz_zip_reader_goto_next_entry(reader);
      }
      if (result == MZ_END_OF_LIST)
        result = MZ_OK;
    }
    if (result == MZ_OK) {
      const std::string destinationUtf8 =
          WideToUtf8(ToExtendedPath(destination));
      result = mz_zip_reader_save_all(reader, destinationUtf8.c_str());
    }
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    if (result == MZ_OK) {
      Notify(notifyWindow, notificationProcessId, true,
             L"ZIPを展開しました: " + destination);
    } else {
      Notify(notifyWindow, notificationProcessId, false,
             ZipErrorMessage(result));
    }
  }).detach();
}

} // namespace sf::win
