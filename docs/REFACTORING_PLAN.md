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

## Phase 2以降(概要・未詳細化)

Phase 1完了後、以下の順で進める想定。着手時に改めてApp.cppの最新状態を調査し、
このドキュメントを更新すること(Phase 1でメソッドが削除され行番号がずれるため)。

### Phase 2: サイドバー（ブックマーク・リンク）

- 対象: `AddCurrentBookmark`, `AddLinkedFolder`, `AddBookmarkForPath`, `AddLink`,
  `RebuildSidebar`, `ActivateSidebarItem`, `EditSidebarItem`, `RemoveSidebarItem`,
  `MoveSidebarItem`
- 主な依存: `sidebar_`(HWND), `sidebarMap_`, `settings_.bookmarks/links`
- 注意点: `ActivateSidebarItem`はA(`Navigate`)とF(`LaunchRegisteredApplication`、
  Phase 3で切り出す可能性)を呼ぶため、依存方向を整理する必要がある。
  `settings_`はF・Dからも参照されるため、Appが所有し続け参照渡しする形が妥当。

### Phase 3: コマンドパレット

- 対象: `RebuildCommandSuggestions`, `MoveCommandSelection`, `AcceptCommandSuggestion`,
  `DismissCommandSuggestions`, `HandleCommandPrefixCharacter`, `AddCommandRegistration`,
  `LaunchRegisteredApplication`
- 主な依存: `commandSuggestions_`, `commandSuggestionItems_`, `commandPrefixBuffer_`等
  (自己完結度は比較的高い)、ただしA/B/Cを横断参照するため最後に近い順序が安全。

### Phase 4: シェルメニュー

- 対象: `ShowFileMenu`, `ShowItemShellMenu`, `ShowBackgroundShellMenu`,
  `AppendFallbackBackgroundMenu`, `ShowLinkMenu`
- 主な依存: `activeShellMenu2_`/`activeShellMenu3_`(メッセージループの`WM_DRAWITEM`等
  から直接参照される点の解消が必要)
- 難易度: 高い(メッセージループとの双方向結合)。

### Phase 5: ナビゲーション・ファイル操作（最終）

- 対象: `Navigate`, `NavigateHistory`, `NavigateUp`, `ShowDrives`, `RefreshPane`,
  `StartSearch`, `SortPane`, `RetireWorker`, `FinishWorker`, `OpenSelected`,
  `BeginRename`, `CopySelection`, `TransferSelectionToOtherPane`, `Paste`,
  `DeleteSelection`, `NewFolder`, `ShowSelectedProperties`, `SelectedPaths`
- `Pane`構造体の実質的な所有者。他の全グループから参照されるため、最後に着手する
  (依存元が先に整理されていることが前提)。

### Phase 6: ウィンドウ管理・メッセージループの整理（最終仕上げ）

- 残った`App`は「ウィンドウ生成・レイアウト・DPI・メッセージルーティング」の
  コーディネーターとして再整理する。`HandleMessage`の巨大switchは、各Phaseで
  切り出したコントローラーへの委譲行が中心になっているはずなので、可読性を
  再確認する。

## 進行管理

- 各Phase完了後、ユーザーの明示的な指示がある場合のみコミットする
  (このプロジェクトのCLAUDE.mdルールに従う)。
- Phaseごとに本ドキュメントの該当セクションを「実装済み」に更新し、次のPhaseの
  詳細化(調査→設計)を行うこと。
