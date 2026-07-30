# App クラスの段階的リファクタリング計画

## 背景

`src/win/App.cpp`(3200行超)の`App`クラスは66個のメソッドを持ち、ウィンドウ管理・
ナビゲーション・ファイル操作・サイドバー・端末起動・シェルメニュー・Zip操作・
コマンドパレット・メッセージループを全て1クラスに抱える「Godクラス」になっている。

一度に全体を分割すると変更量・レビュー量・回帰リスクが大きすぎるため、
**自己完結度の高い機能から段階的に切り出す**方針を取る。各Phaseは独立してコミット・
検証できる単位とし、既存の振る舞いは一切変更しない(リファクタリングのみ)。

## 機能グループと依存関係マップ

調査の結果、以下6グループに分類できる。グループ間の依存が想像以上に強いため、
分割順序は「自己完結度が高い順」とする。

| グループ | 主なメソッド | 自己完結度 |
|---|---|---|
| E. Zip操作 | `CreateZipFromSelection`, `ExtractSelectedZip` | 高（Phase 1） |
| C. 端末起動 | `ShowTerminalMenu`, `LaunchSelectedTerminal` | 高（Phase 1、ステートレス） |
| B. サイドバー | `AddCurrentBookmark`, `RebuildSidebar`, `ActivateSidebarItem` 等 | 中 |
| D. シェルメニュー | `ShowFileMenu`, `ShowItemShellMenu`, `ShowBackgroundShellMenu` 等 | 中〜低（メッセージループと双方向結合） |
| F. コマンドパレット | `RebuildCommandSuggestions`, `AcceptCommandSuggestion` 等 | 低（A/B/Cを横断参照） |
| A. ナビゲーション・ファイル操作 | `Navigate`, `ShowDrives`, `CopySelection` 等 | 低（`Pane`構造体の実質的な所有者、他グループから頻繁に参照される） |
| — ウィンドウ管理・レイアウト | `CreateControls`, `LayoutControls`, `ApplyDpi` 等 | App本体に残す想定 |
| — メッセージループ | `HandleMessage`, `WindowProcedure` | App本体に残す想定（各グループへのルーター） |

### 主な相互依存(グループ間)

- B `ActivateSidebarItem` → A `Navigate`, F `LaunchRegisteredApplication`
- F `AcceptCommandSuggestion` → A `Navigate`/`StartSearch`, C `LaunchSelectedTerminal`, F `LaunchRegisteredApplication`
- F `RebuildCommandSuggestions` → B `settings_.bookmarks/links`（読取）, A/C `panes_[activePane_]`（読取）
- D `ShowFileMenu` → A `SelectedPaths`/`RefreshPane`/`OpenSelected`/`BeginRename`, F `LaunchRegisteredApplication`
- E `CreateZipFromSelection`/`ExtractSelectedZip` → A `SelectedPaths`
- C `ShowTerminalMenu`/`LaunchSelectedTerminal` → A `panes_[activePane_].path`等（読取のみ）
- メッセージループ → 全グループ（`WM_COMMAND`の巨大switchが実質的なディスパッチャ）

### 分割の難所

1. **`Pane`構造体**が最大の共有点。所有者はA(ナビゲーション)であるべきだが、
   C/D/Fが`path`/`searchMode`/`searchRoot`を直接読み取っている。
2. **`activeShellMenu2_`/`activeShellMenu3_`**はD(シェルメニュー)の状態だが、
   `HandleMessage`の`WM_DRAWITEM`/`WM_INITMENUPOPUP`/`WM_MENUCHAR`からも直接参照される。
3. **`settings_`**はB(サイドバー)専属に見えて、F・Dからも参照される。
4. **`SelectedPaths()`**はA所属だが、D・E・Fの中心的な依存先。
5. **`Notify`, `PromptText`, `SaveSettings`, `UpdateActivePaneVisuals`**はどのグループにも
   属さない汎用ヘルパーで、全グループから呼ばれる。App本体（コーディネーター）に残す。

### 設計の基本原則(全Phase共通)

- コントローラークラスに`App&`を渡さない。`window_`・選択パス・アクティブペイン情報・
  通知/更新コールバックは、呼び出しの都度メソッド引数として渡す。
  理由: `window_`はコンストラクタ時点(`WM_NCCREATE`前)では未確定であり、
  `activePane_`は操作ごとに変わるため、コンストラクタで固定依存を持たせられない。
- 過度な抽象化(共通インターフェース、コンテキストオブジェクトの新設等)は避ける(YAGNI)。
  必要な値は個別の引数か、小さな値構造体(例: `TerminalMenuIds`)で渡す。
- 各Phaseはビルドが常に通る中間ステップに分け、都度`ctest`で検証する。

---

## Phase 1: ZipController / TerminalController の切り出し（実装済み）

最も自己完結度が高い2機能から着手する。

実装日: 2026-07-30。`ZipController`と`TerminalController`への切り出し、
`PickFolder`の`WinUtils.h`への移設、`App`からの委譲、CMakeへの追加まで完了。
Windows x64 Releaseビルドと全CTest（UIスモークを含む6件）の通過を確認済み。

### 新規ファイル

#### `src/win/ZipController.h` / `.cpp`

```cpp
// ZipController.h
#pragma once
#include <windows.h>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sf::win {

class ZipController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;
  using RefreshPaneFn = std::function<void(int pane)>;

  ZipController() = default;

  void CreateZipFromSelection(HWND window,
                              const std::vector<std::wstring> &paths,
                              const NotifyFn &notify);
  void ExtractSelectedZip(HWND window,
                          const std::vector<std::wstring> &paths,
                          const NotifyFn &notify);
  void HandleZipDone(LPARAM lParam, int activePane, const NotifyFn &notify,
                     const RefreshPaneFn &refreshPane);

  [[nodiscard]] std::size_t PendingOperationCount() const {
    return pendingZipOperations_;
  }

private:
  std::size_t pendingZipOperations_ = 0;
};

} // namespace sf::win
```

`.cpp`は現行の`App::CreateZipFromSelection`/`App::ExtractSelectedZip`
(App.cpp、`void App::CreateZipFromSelection()`で検索)のロジックをそのまま移植し、
`Notify(...)`呼び出しを`notify(...)`(第2引数を明示、`std::function`はデフォルト引数を
持てない)に変える。`HasZipExtension`(App.cpp先頭の匿名namespace内)もこの`.cpp`に移す。
`HandleZipDone`は現行の`kMessageZipDone`ケースの`reinterpret_cast<ZipResult*>`と
減算・通知・`RefreshPane`呼び出しロジックを移す(`SetFocus`呼び出しはApp側に残す、
UI全体のフォーカス管理のため)。

#### `src/win/TerminalController.h` / `.cpp`

```cpp
// TerminalController.h
#pragma once
#include "win/TerminalLauncher.h"
#include <windows.h>
#include <functional>
#include <string>

namespace sf::win {

struct TerminalMenuIds final {
  UINT commandPrompt = 0;
  UINT commandPromptAdmin = 0;
  UINT powerShell = 0;
  UINT powerShellAdmin = 0;
};

class TerminalController final {
public:
  using NotifyFn = std::function<void(const std::wstring &message, bool error)>;

  TerminalController() = default;

  void ShowTerminalMenu(HWND window, HWND sourceButton, bool enabled,
                        const TerminalMenuIds &ids) const;
  void LaunchSelectedTerminal(HWND window, const std::wstring &directory,
                              TerminalKind kind, bool administrator,
                              const NotifyFn &notify) const;
};

} // namespace sf::win
```

`.cpp`は現行の`App::ShowTerminalMenu`/`App::LaunchSelectedTerminal`のロジックを
そのまま移植する。`directory`(実効パス)と`enabled`(パス有無)はApp側で
`panes_[activePane_]`から計算して渡すため、`TerminalController`は`Pane`構造体を
一切知らなくてよい。`IdCmd`等の実コマンドIDはApp.cpp側`ControlId` enumの値そのものを
呼び出し時に`TerminalMenuIds`として渡す(新規共有ヘッダーは作らない)。

#### `src/win/WinUtils.h` への追記

現行App.cpp内`PickFolder`(Zip専用ではなく`AddLinkedFolder`からも使われる共用ヘルパー)を
そのままinline関数として移す。`#include <shobjidl.h>`の追加が必要
(`IFileOpenDialog`等のため)。

### 変更ファイル

#### `src/win/App.h`
- `#include "win/TerminalController.h"` / `#include "win/ZipController.h"` を追加
- private宣言から `ShowTerminalMenu`, `LaunchSelectedTerminal`,
  `CreateZipFromSelection`, `ExtractSelectedZip` を削除
  (委譲ラッパーは作らない — 呼び出し元で直接
  `zipController_.Xxx(...)`/`terminalController_.Xxx(...)`を呼ぶ)
- private メンバーから `pendingZipOperations_` を削除、代わりに
  `ZipController zipController_;` / `TerminalController terminalController_;` を追加
  (`pendingFileOperations_`の直後が既存の並びと自然)
- (任意)重複削減のため `[[nodiscard]] std::wstring ActivePaneEffectivePath() const;`
  を追加してもよい。追加する場合はTerminalController呼び出し箇所
  (4箇所+`AcceptCommandSuggestion`)にのみ適用し、既存の他箇所
  (`AddCurrentBookmark`等)はスコープ外として変更しない

#### `src/win/App.cpp`
- 匿名namespace内の `HasZipExtension`, `PickFolder` を削除
- `App::CreateZipFromSelection`/`App::ExtractSelectedZip` を削除
- `App::ShowTerminalMenu`/`App::LaunchSelectedTerminal` を削除
- `WM_COMMAND`の`IdTerminal`/`IdCmd`/`IdCmdAdmin`/`IdPowerShell`/`IdPowerShellAdmin`/
  `IdZipCreate`/`IdZipExtract`ケースを、`zipController_`/`terminalController_`への
  委譲呼び出しに変更
- `AcceptCommandSuggestion`内の`CommandSuggestionKind::Terminal`分岐を
  `terminalController_.LaunchSelectedTerminal(...)`呼び出しに変更
- `kMessageZipDone`ケースを`zipController_.HandleZipDone(...)`への委譲に変更
  (`SetFocus`はApp側に残す)
- `WM_CLOSE`の合算式を`pendingFileOperations_ + zipController_.PendingOperationCount()`
  に変更
- `#include "win/ZipOperations.h"`が不要になるか確認して削除、
  `#include "win/ZipController.h"`/`#include "win/TerminalController.h"`を追加

#### `CMakeLists.txt`
`add_executable(SimpleFiler WIN32 ...)`のソースリスト(既存はアルファベット順)に
`src/win/TerminalController.cpp`と`src/win/ZipController.cpp`を追加。

### 実装順序(各ステップでビルドが通ることを確認)

1. `WinUtils.h`に`PickFolder`を追加し、同じステップでApp.cpp内の同名定義を削除
   (同一namespace内の重複定義を避けるため同時に行う)。`HasZipExtension`はまだ
   App.cpp内に残す。→ビルド確認
2. `ZipController.h/.cpp`を新規作成(`HasZipExtension`もここに含める)。
   CMakeLists.txtに追加。この時点ではApp.cpp側の元メソッドはまだ削除しない
   (新旧並存、まだ誰も新クラスを呼ばない)。→ビルド確認
3. App.h/App.cppを変更: `zipController_`メンバー追加、Zip関連メソッド宣言・実装削除、
   `pendingZipOperations_`削除、`IdZipCreate`/`IdZipExtract`/`kMessageZipDone`/
   `WM_CLOSE`を委譲に変更。→ビルド確認 + ctest実行
4. `TerminalController.h/.cpp`を新規作成。CMakeLists.txtに追加。→ビルド確認
5. App.h/App.cppを変更: `terminalController_`メンバー追加、端末関連メソッド宣言・
   実装削除、`IdTerminal`/`IdCmd`系/`AcceptCommandSuggestion`を委譲に変更。
   →ビルド確認 + ctest実行
6. 最終確認: 不要include(`ZipOperations.h`等)の精査・削除、全体ビルド+ctest+
   実機での手動確認

### 検証

1. `cmake --build --preset windows-x64` でビルド成功を各ステップで確認
2. `ctest --preset windows-x64` で既存テスト(UIスモーク含む)が全て通ることを確認
3. 実機でSimpleFiler.exeを起動し、以下を手動確認:
   - ファイル選択→ZIP作成→保存ダイアログ→作成成功・ステータス表示・ペイン更新
   - .zipファイル選択→ZIP展開→フォルダー選択ダイアログ→展開成功
   - Zip操作中にウィンドウを閉じようとして「処理が進行中」警告が出る
   - 「端末▾」ボタンでCMD/PowerShell(通常/管理者)の4項目が表示され起動できる
   - アクティブペインがパス未設定(ドライブ一覧)のとき端末メニューがグレーアウトされる
   - コマンドパレットで`cmd`/`cmd admin`と入力し候補選択で端末が正しいディレクトリで
     起動する
   - フォルダーリンク登録(「リンク＋」→フォルダー)で`PickFolder`ダイアログが正常動作
     する(WinUtils.hへ移設した副作用がないか)

---

## Phase 2: SidebarController の切り出し（実装済み）

Phase 1完了後の`App.cpp`を再調査した結果、サイドバーの表示項目と設定配列の対応を表す
`sidebarMap_`は、登録の追加・編集・削除・並べ替えだけでなく右クリックメニューの活性状態
判定にも使われている。この対応表を`App`に公開せず`SidebarController`が所有するため、
当初の対象メソッドに右クリックメニュー処理も加えて一体で切り出す。

実装日: 2026-07-30。`SidebarController`への切り出し、右クリックメニュー処理と
`sidebarMap_`の移設、共通パスヘルパーの`WinUtils.h`への移設、CMakeへの追加まで完了。
Windows x64 Releaseビルドと全CTest（UIスモークを含む6件）の通過を確認済み。

### 境界と依存方向

- `AppSettings`はコマンドパレット・シェルメニュー・アプリ起動からも参照されるため、
  引き続き`App`が所有し、必要な操作へ参照渡しする。
- `SidebarController`は`sidebarMap_`だけを所有する。`HWND`や`AppSettings&`を
  コンストラクタで保持せず、操作時の引数として受け取る。
- `PromptText`、`SaveSettings`、`Notify`はコールバックで受け取る。
- ブックマークの起動は`NavigateFn`、アプリリンクの起動は
  `LaunchApplicationFn`へ委譲する。これにより`Pane`や
  `LaunchRegisteredApplication`の実装を知らない。
- ファイルリンクは自己完結しているため、現行どおり`OpenPath`で直接起動する。
- `AddCurrentBookmark`は`App`側で`ActivePaneEffectivePath()`を計算し、
  `SidebarController::AddBookmarkForPath`を直接呼ぶ。委譲ラッパーは残さない。
- `ShowLinkMenu`はPhase 4のシェルメニュー対象なので今回は`App`に残す。

### 新規ファイル

#### `src/win/SidebarController.h` / `.cpp`

公開操作は以下とする。

- `AddBookmarkForPath`
- `AddLinkedFolder`
- `AddLink`
- `RebuildSidebar`
- `ActivateSidebarItem`
- `EditSidebarItem`
- `RemoveSidebarItem`
- `MoveSidebarItem`
- `ShowContextMenu`

右クリックメニューの実コマンドIDは`App.cpp`の`ControlId`を共有ヘッダーへ移さず、
`SidebarMenuIds`として呼び出し時に渡す。`sidebarMap_`は
`std::vector<std::pair<bool, std::size_t>>`としてprivateメンバーへ移す。

`LeafName`、`SplitKeywords`、`JoinKeywords`はサイドバー登録処理だけで使われるため
`SidebarController.cpp`の匿名namespaceへ移す。`ResolveAppPath`と`MakeAppPath`は
コマンドパレット・アプリ起動からも使われるため、`WinUtils.h`の共通inline関数へ移す。

### 変更ファイル

#### `src/win/App.h` / `.cpp`

- `SidebarController`をメンバーとして追加する。
- 対象9メソッドの宣言・実装と`sidebarMap_`を削除する。
- 初期表示、`WM_COMMAND`、サイドバーのダブルクリック、
  `kMessageSidebarMove`をコントローラーへの直接委譲に変更する。
- `WM_CONTEXTMENU`のサイドバー分岐を`ShowContextMenu`への委譲に置き換える。
- `AddCommandRegistration`のフォルダー・アプリ登録もコントローラーへ直接委譲する。

#### `src/win/WinUtils.h`

`ResolveAppPath`と`MakeAppPath`を追加し、`App.cpp`内の同名定義を削除する。

#### `CMakeLists.txt`

`src/win/SidebarController.cpp`を`SimpleFiler`のソースへ追加する。

### 検証

1. `build.bat test`でWindows x64 Releaseビルドと全CTestを実行する。
2. UIスモークで起動・サイドバー描画・基本操作に回帰がないことを確認する。
3. 実機で追加（フォルダー・ファイル・アプリ）、編集、削除、並べ替え、
   通常起動・管理者起動、存在しないリンクの警告を確認する。

---

## Phase 3: CommandController の切り出し（実装済み）

Phase 2完了後の`App.cpp`を再調査した。候補リストとプレフィックス入力の状態は
自己完結している一方、候補確定後のフォルダー移動・アプリ起動・端末起動、
新規登録では他コントローラーを横断する。`CommandController`自身が他コントローラーを
所有せず、操作結果をコールバックで`App`へ返す境界とする。

実装日: 2026-07-30。候補・プレフィックス状態、候補構築と確定、登録アプリ起動を
`CommandController`へ移し、`SidebarController`と`TerminalController`への接続を
`App`のコールバックへ整理した。Windows x64 Releaseビルドと全CTest
（候補移動・破棄を追加したUIスモークを含む6件）の通過を確認済み。

### 対象と所有状態

- 対象: `RebuildCommandSuggestions`, `MoveCommandSelection`,
  `AcceptCommandSuggestion`, `DismissCommandSuggestions`,
  `HandleCommandPrefixCharacter`, `AddCommandRegistration`,
  `LaunchRegisteredApplication`
- `CommandSuggestionKind`と`CommandSuggestion`を`App`から移す。
- `commandSuggestionItems_`、`commandPrefixBuffer_`、
  `commandPrefixSource_`、`commandPrefixTick_`をコントローラーへ移す。
- `searchEdit_`と`commandSuggestions_`はレイアウト・DPI・生成を`App`が担うため、
  `App`に残して操作時に渡す。
- `AppSettings`も引き続き`App`が所有し、候補構築・アプリ起動へ参照渡しする。

### 依存方向

- フォルダー候補の確定は、対象パスと別ペイン指定を`NavigateFn`へ渡す。
- アプリ候補の確定とサイドバー・ファイルメニューからのアプリ起動は、
  `CommandController::LaunchRegisteredApplication`へ集約する。
- アプリ引数展開に必要な選択パスと左右・反対側のフォルダーは、既存の
  `AppArgumentContext`を`App`で組み立てて渡す。`CommandController`は`Pane`を知らない。
- 端末候補の確定は`LaunchTerminalFn`へ委譲し、`App`から
  `TerminalController`へ接続する。
- 検索文字列がコマンドでない場合の検索開始は`StartSearchFn`へ委譲する。
- `AddCommandRegistration`は登録種別を判定するところまで担当し、
  `AddBookmarkFn`または`AddApplicationFn`へ委譲する。`App`から
  `SidebarController`へ接続する。
- 通知、アクティブペイン更新、フォーカス復元はコールバックで`App`へ戻す。

### 呼び出し元の変更

- `WM_COMMAND`の検索欄変更、候補ダブルクリック、検索ボタンを委譲する。
- `kMessageCommandType`、`kMessageCommandAccept`、`kMessageCommandMove`,
  `kMessageCommandDismiss`、`kMessageCommandNew`を委譲する。
- アドレス欄フォーカス時の候補非表示・クリアもコントローラー操作へ置き換える。
- サイドバーのアプリリンク起動コールバックとファイルメニューの登録アプリ起動を
  新コントローラーへ接続する。

### 新規・変更ファイル

- `src/win/CommandController.h` / `.cpp`を追加する。
- `App.h`から対象メソッド・候補型・候補/プレフィックス状態を削除し、
  `CommandController`メンバーを追加する。
- `App.cpp`を直接委譲へ変更する。
- `CMakeLists.txt`へ`src/win/CommandController.cpp`を追加する。

### 検証

1. `ff`、`aa`、`cmd`の候補構築・絞り込み・上下移動・確定・キャンセル。
2. Ctrl/Shift修飾による管理者起動、選択パス省略、反対ペインでのフォルダー表示。
3. コマンド入力でない場合の通常検索。
4. `ff`/`aa`入力後の新規登録とサイドバー即時更新。
5. Windows x64 Releaseビルドと全CTest（UIスモークを含む）。

---

## Phase 4: ShellMenuController の切り出し（実装済み）

Phase 3完了後の実装を再調査した。シェル拡張が生成するメニューは表示中だけ
`IContextMenu2`/`IContextMenu3`へWindowsメッセージを転送する必要があり、
背景メニューは「新規作成」などの動的状態を維持するためフォルダー単位で
`IContextMenu`をキャッシュしている。このCOM状態と転送処理を同じコントローラーへ
まとめる。

実装日: 2026-07-30。シェルメニュー生成、シェル拡張へのメッセージ転送、
背景メニューのCOMキャッシュと専用COM/PIDLヘルパーを`ShellMenuController`へ移した。
`App`はコマンドID・選択パス・実効フォルダーとコールバックを渡すだけになり、
Windows x64 Releaseビルドと全CTest（UIスモークを含む6件）の通過を確認済み。

### 対象と所有状態

- 対象: `ShowFileMenu`, `ShowItemShellMenu`, `ShowBackgroundShellMenu`,
  `AppendFallbackBackgroundMenu`, `ShowLinkMenu`
- `activeShellMenu2_`、`activeShellMenu3_`、
  `cachedBackgroundMenuFolder_`、`cachedBackgroundMenu_`を移す。
- `ShellMenuController`のデストラクタで背景メニューのキャッシュを解放する。
- `ComPtr`、`PidlDeleter`、`UniquePidl`はシェルメニュー専用になっているため、
  `ShellMenuController.cpp`の匿名namespaceへ移す。

### メッセージループとの接続

- `HandleMenuMessage(UINT, WPARAM, LPARAM, LRESULT&)`を公開する。
- `WM_INITMENUPOPUP`、`WM_UNINITMENUPOPUP`、`WM_MEASUREITEM`、
  シェル側の`WM_DRAWITEM`を`IContextMenu2::HandleMenuMsg`へ転送する。
- `WM_MENUCHAR`は`IContextMenu3::HandleMenuMsg2`の戻り値を`LRESULT&`で返す。
- `App`は自前コントロールの`WM_DRAWITEM`処理後にこのメソッドへ委譲する。
- `TrackPopupMenu`中だけactiveポインターを設定するスコープガードは
  コントローラー内部へ移し、早期returnでも必ず解除する。

### 依存方向

- 実コマンドIDは共有enumへ移さず、`ShellMenuIds`として呼び出し時に渡す。
  シェルコマンド範囲、組み込みファイル操作、リンク追加、登録アプリ基点を含める。
- 選択パス、背景フォルダー、ドライブ表示状態、`AppSettings`は引数で受け取る。
- シェルコマンド後の更新は`RefreshPaneFn`、`open`/`rename`の標準動詞は
  `OpenSelectedFn`/`BeginRenameFn`へ委譲する。
- フォールバックメニューの組み込みコマンドは現行どおり`WM_COMMAND`へ戻す。
- 登録アプリの選択は`LaunchApplicationFn`へ委譲し、`App`から
  `CommandController::LaunchRegisteredApplication`へ接続する。
- `ShellMenuController`は`Pane`、`CommandController`、`App`を直接参照しない。

### 呼び出し元の変更

- ファイル一覧の右クリックとAppsキーから、選択パス・実効フォルダー・状態を渡して
  `ShowFileMenu`を直接呼ぶ。
- ツールバーのリンクメニューを`ShowLinkMenu`へ委譲する。
- `App`のデストラクタから背景メニュー解放処理を削除する。
- メッセージループのシェルメニュー転送分岐を`HandleMenuMessage`へ置き換える。

### 新規・変更ファイル

- `src/win/ShellMenuController.h` / `.cpp`を追加する。
- `App.h`から対象メソッドとCOM状態を削除し、コントローラーメンバーを追加する。
- `App.cpp`からシェルメニュー実装・専用COMヘルパーを移し、直接委譲へ変更する。
- `CMakeLists.txt`へ`src/win/ShellMenuController.cpp`を追加する。

### 検証

1. 単一・複数選択の標準シェルメニュー、open、rename、拡張動詞。
2. 背景メニューの貼り付け・新規作成と、取得失敗時のフォールバック。
3. クロスフォルダー検索結果でのフォールバックメニュー。
4. 登録アプリサブメニューとリンク追加メニュー。
5. owner-draw/cascading shell extensionのメッセージ転送。
6. Windows x64 Releaseビルドと全CTest（UIスモークを含む）。

---

## Phase 5以降

### Phase 5: PaneController / FileOperationController の切り出し（実装済み）

Phase 4完了後の`App`を再調査した。ナビゲーションだけを移しても、非同期列挙結果、
仮想リスト表示、選択復元、検索中止、設定の読み書きが`Pane`の内部状態を直接更新するため、
`Pane`の所有権が`App`に残ってしまう。一方、コピー・移動・削除などはペイン状態そのものを
所有する必要はなく、操作開始数と完了通知だけが共通状態である。このためPhase 5では
`PaneController`と`FileOperationController`に分ける。

実装日: 2026-07-30。`Pane`、worker寿命、ナビゲーション、検索、ソート、
仮想リスト表示と列挙完了処理を`PaneController`へ移し、ファイル操作と進行中件数を
`FileOperationController`へ移した。`App`から`Pane`・worker・`FileItem`への直接参照を
削除し、既存の全コントローラーを値とコールバックで接続した。
Windows x64 Releaseビルドと全CTest（UIスモークを含む6件）の通過を確認済み。

#### PaneControllerの対象と所有状態

- 対象: `Navigate`, `NavigateHistory`, `NavigateUp`, `ShowDrives`, `RefreshPane`,
  `StartSearch`, `SortPane`, `RetireWorker`, `FinishWorker`, `OpenSelected`,
  `BeginRename`, `SelectedPaths`, `ActivePaneEffectivePath`
- `Pane`と`Pane::RetiredWorker`、左右2ペインの配列を`PaneController`へ移す。
- パス、検索条件、ドライブ表示、busy、隠しファイル表示、ソート、履歴、
  `FileItem`、generation、実行中・退役済み`jthread`を一括して所有する。
- `PaneController`のデストラクタで現行・退役workerへ停止を要求する。
- アドレス欄とリストの`HWND`はウィンドウ生成・レイアウトを担う`App`が作成し、
  `AttachControls(pane, address, list)`で非所有参照として登録する。
- `AddressHandle`、`ListHandle`、`PaneIndexFromControl`の小さなアクセサーを公開し、
  `App`のレイアウト、DPI、フォーカス処理は引き続きハンドルだけを扱う。
- `ApplySettings`/`WriteSettings`で`showHidden`・ソート・現在パスを受け渡しし、
  `App`から内部状態への直接アクセスをなくす。

#### 列挙・検索・リスト通知

- `kMessageEnumerationBatch`と`kMessageEnumerationDone`は
  `HandleEnumerationBatch`/`HandleEnumerationDone`へ委譲する。
  generation判定、item追加、worker回収、ソート、ListView更新までコントローラー内で行う。
- `LVN_GETDISPINFO`の`FileItem`参照と`LVN_COLUMNCLICK`のソート状態更新も委譲する。
  `App`が`items`を直接読む経路を残さない。
- `LVN_ENDLABELEDIT`だけは確定した「元パス・新しい名前」を
  `FileOperationController::RenameItem`へ渡す必要があるため、
  `PaneController::ItemPath(pane, index)`で不変のパス値を取得する。
- 検索中止を`CancelSearch(pane)`として追加し、現在`WM_COMMAND`内にある
  worker停止・generation更新・busy解除を移す。
- 検索開始/終了時の検索ボタン表示と通知は`SearchStateFn`/`NotifyFn`で`App`へ返す。
  検索欄自体はコマンドパレットと共有するため`App`に残す。

#### FileOperationControllerの対象と所有状態

- 対象: `CopySelection`, `TransferSelectionToOtherPane`, `Paste`,
  `DeleteSelection`, `NewFolder`, `ShowSelectedProperties`とラベル編集後のrename開始。
- `pendingFileOperations_`を移し、`PendingOperationCount()`を公開する。
- 各操作は`Pane`を参照せず、選択パス、コピー先/実効フォルダー、2ペイン表示状態を
  値として受け取る。選択とフォルダーのスナップショットは`App`が
  `PaneController`から取得して渡す。
- `NewFolder`は`PromptTextFn`を受け取り、名前の空判定・禁止文字検証・非同期開始を
  コントローラー内で行う。
- `HandleOperationDone(LPARAM, NotifyFn, RefreshPaneFn)`で結果オブジェクトを回収し、
  pending数の減算、通知、成功時の左右ペイン更新を行う。
- 操作後のフォーカス復元はウィンドウ全体の責務なので`App`に残す。

#### 依存方向と呼び出し元

- `PaneController`/`FileOperationController`に`App&`や他コントローラーを渡さない。
  `HWND`、値、通知/状態更新コールバックだけを操作時に渡す。
- `BuildAppArgumentContext`は複数コントローラーを接続する小さな組み立て処理として
  `App`に残し、`PaneController`の選択パス・左右の実効フォルダーから構築する。
- `CommandController`、`SidebarController`、`TerminalController`,
  `ShellMenuController`、`ZipController`への既存コールバックは、
  `App`経由で`PaneController`の公開操作・アクセサーへつなぎ替える。
- `WM_COMMAND`の移動・更新・検索・ファイル操作、アドレス確定、
  リストのダブルクリック/Enter/右クリック、列挙・操作完了メッセージを委譲する。
- `WM_CLOSE`の進行中件数は
  `fileOperationController_.PendingOperationCount()`と
  `zipController_.PendingOperationCount()`を合算する。

#### 新規・変更ファイル

- `src/win/PaneController.h` / `.cpp`を追加する。
- `src/win/FileOperationController.h` / `.cpp`を追加する。
- `App.h`から`Pane`、対象メソッド、`panes_`、`pendingFileOperations_`を削除し、
  2コントローラーのメンバーを追加する。
- `App.cpp`の対象実装と列挙/操作完了処理を移し、直接委譲へ変更する。
- `CMakeLists.txt`へ新しい`.cpp` 2ファイルを追加する。

#### 実装順序

1. `FileOperationController`を追加し、ファイル操作とpending数、完了メッセージを移す。
   この時点では`App::SelectedPaths`と実効フォルダーを引数として渡す。
2. `PaneController`へ`Pane`とworker寿命管理、選択・実効パスの読み取りを移す。
   `App`のコントロール生成・レイアウトをハンドルアクセサーへ置き換える。
3. ナビゲーション、ドライブ一覧、履歴、更新、検索開始/中止を移す。
4. 列挙batch/doneとソート、仮想リスト表示、選択復元を移す。
5. `OpenSelected`/`BeginRename`と各コントローラーのコールバックを接続する。
6. `App`に残る`panes_`/`Pane`/worker/item直接参照がゼロであることを`rg`で確認し、
   不要includeを削除して全体を検証する。

#### 検証

1. 通常フォルダー・ドライブ一覧・戻る/進む/上へ・左右ペイン・履歴分岐。
2. 通常列挙と検索の開始/中止/再実行、古いgenerationの結果破棄、終了時worker回収。
3. 名前・サイズ・更新日時の昇降順、ソート後の選択・フォーカス復元。
4. 隠しファイル表示、設定保存と再起動後のパス・ソート状態復元。
5. 開く、rename、新規フォルダー、コピー/切り取り/貼り付け、反対ペインへの
   コピー/移動、通常/完全削除、プロパティ。
6. ファイル操作完了/失敗/キャンセル通知、成功時の両ペイン更新、
   操作中終了警告。
7. Windows x64 Releaseビルドと全CTest（UIスモークを含む）。

### Phase 6: ウィンドウ管理・メッセージループの整理（実装済み）

Phase 5完了後の`App`には、ウィンドウ/コントロールの生成と配置、DPI・描画、
アクティブペインとフォーカス、設定保存、ダイアログ、各コントローラー間の接続が残った。
これらはアプリケーションシェルとして妥当だが、`HandleMessage`は約800行あり、
メッセージ種別ごとの境界が不明瞭である。最終Phaseでは新しい機能コントローラーを
増やさず、`App`内部のルーティングを責務別のprivateハンドラーへ整理する。

実装日: 2026-07-30。owner-draw、色、splitter、`WM_COMMAND`、`WM_NOTIFY`、
`kMessage*`の処理を6つのprivateハンドラーへ分割し、共通するナビゲーション、
更新、検索、選択項目の起動、登録アプリ起動、候補確定を小さなprivateメソッドへ
整理した。メッセージ転送順序と戻り値を維持したまま、`HandleMessage`を約800行から
約160行へ縮小した。Windows x64 Releaseビルドと全CTest（UIスモークを含む6件）の
通過を確認済み。

#### 残す責務

- `RegisterClasses`, `CreateMainWindow`, `CreateControls`,
  `CreatePaneControls`, `LayoutControls`, `ApplyDpi`
- `UpdateActivePaneVisuals`, `UpdatePaneSearchState`,
  `RestorePaneFocusIfNeeded`, `CreateAccelerators`
- `InitializeFromSettings`, `VerifySettingsWritable`, `SaveSettings`
- `PromptText`, `ShowAboutDialog`, `Notify`, `BuildAppArgumentContext`
- `activePane_`, 2ペイン/サイドバー表示、splitter、フォント・ブラシ・各`HWND`
- `WindowProcedure`と最上位の`HandleMessage`

これらは複数コントローラーを調停するか、トップレベルウィンドウの寿命に属するため、
別クラスへ移しても依存が循環するだけである。

#### HandleMessageの分割

- `HandleCommand(WPARAM, LPARAM)`:
  `WM_COMMAND`の事前処理と`ControlId`別のコントローラー呼び出しを担当する。
- `HandleNotify(LPARAM)`:
  ペインのフォーカス/右クリック/キー/列/表示/rename通知を担当する。
- `HandleOwnerDraw(WPARAM, LPARAM, LRESULT&)`:
  ツールバーボタンとサイドバーのowner-drawを担当し、未処理なら
  `ShellMenuController::HandleMenuMessage`へ戻せる形にする。
- `HandleControlColor(UINT, WPARAM, LPARAM, LRESULT&)`:
  edit/list/staticの色処理をまとめる。
- `HandleSplitterMessage(UINT, WPARAM, LPARAM, LRESULT&)`:
  drag開始・移動・終了とカーソル変更をまとめる。
- `HandleAppMessage(UINT, WPARAM, LPARAM, LRESULT&)`:
  `kMessageCommand*`、サイドバー移動、フォーカス復元、アドレス確定、検索、
  列挙/ファイル操作/ZIP完了をまとめる。
- 各ハンドラーは処理したかを`bool`で返し、必要な戻り値を`LRESULT&`へ設定する。
  `HandleMessage`はWindows標準メッセージの最上位ルーティングと
  `DefWindowProcW`へのフォールバックだけを残す。

#### コールバックと依存方向

- 現在`HandleMessage`先頭で全メッセージについて生成している通知、検索状態、
  ナビゲーション、更新、選択を開く各ラムダは、必要なハンドラー内だけで生成する。
- 共通化が必要な場合も`App&`を渡すコンテキスト構造体は作らず、privateメソッドか
  小さなラムダに留める。
- `ControlId`と`kMessage*`は現在のファイルに残す。共有enumの新設は行わない。
- コントローラー同士を直接参照させず、引き続き`App`が値とコールバックを接続する。
- シェルメニュー表示中の`WM_DRAWITEM`/`WM_MENUCHAR`等の転送順序、
  owner-drawの戻り値、`PostMessageW(kMessageRestoreFocus)`のタイミングを維持する。

#### 整理対象

- `App.cpp`の不要includeと、移管後に不要になった匿名namespaceヘルパーを再確認する。
- `App.h`のメソッドとメンバーを「window resources / UI state /
  controllers / settings」の順に並べる。
- 既存の文字列、コマンドID、アクセラレーター、フォーカス動作は変更しない。
- 完了条件として`HandleMessage`の各caseが短い委譲かトップレベル処理になり、
  `App`にファイル操作・ペイン内部状態・各機能固有状態が戻っていないことを確認する。

#### 実装順序

1. owner-draw、色、splitterの純粋なUI分岐をprivateハンドラーへ移す。
2. `WM_NOTIFY`を`HandleNotify`へ移し、ペイン通知の戻り値を保持する。
3. `WM_COMMAND`を`HandleCommand`へ移し、既存コントローラー接続を保持する。
4. `kMessage*`を`HandleAppMessage`へ移す。
5. `HandleMessage`先頭のラムダを必要なハンドラーへ局所化し、
   標準メッセージとシェルメニュー転送を整理する。
6. 不要include・宣言を削除し、差分レビューと全体検証を行う。

#### 検証

1. 起動、リサイズ、最大化、DPI変更、サイドバー/2ペイン切替、splitter操作。
2. ツールバーボタン、サイドバー、アクティブペインのowner-drawと色。
3. キーボードアクセラレーター、アドレス/検索フォーカス、フォーカス復元。
4. コマンドパレット、シェルメニュー、各非同期完了メッセージの戻り値と転送。
5. 設定保存、終了時の進行中操作警告、About/入力ダイアログ。
6. Windows x64 Releaseビルドと全CTest（UIスモークを含む）。

## 完了状態

Phase 1からPhase 6までの計画をすべて実装した。`App`はトップレベルウィンドウ、
UI状態、設定、各コントローラー間の調停を担い、ZIP、端末、サイドバー、コマンド、
シェルメニュー、ペイン、ファイル操作の機能固有状態と処理は各コントローラーへ
分離されている。

## 進行管理

- 各Phase完了後、ユーザーの明示的な指示がある場合のみコミットする
  (このプロジェクトのCLAUDE.mdルールに従う)。
- Phaseごとに本ドキュメントの該当セクションを「実装済み」に更新し、次のPhaseの
  詳細化(調査→設計)を行うこと。
