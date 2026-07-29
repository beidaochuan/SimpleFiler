param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class SimpleFilerNativeMethods
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GUITHREADINFO
    {
        public int cbSize;
        public uint flags;
        public IntPtr hwndActive;
        public IntPtr hwndFocus;
        public IntPtr hwndCapture;
        public IntPtr hwndMenuOwner;
        public IntPtr hwndMoveSize;
        public IntPtr hwndCaret;
        public RECT rcCaret;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr parent, int id);

    private delegate bool EnumThreadWindowCallback(
        IntPtr window, IntPtr parameter);
    private delegate bool EnumWindowCallback(
        IntPtr window, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumThreadWindows(
        uint threadId, EnumThreadWindowCallback callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(
        EnumWindowCallback callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr window, uint command);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(
        IntPtr window, StringBuilder className, int maximum);

    public static IntPtr FindThreadWindow(uint threadId, string className)
    {
        IntPtr result = IntPtr.Zero;
        EnumThreadWindows(threadId, delegate(IntPtr window, IntPtr parameter)
        {
            StringBuilder value = new StringBuilder(256);
            GetClassName(window, value, value.Capacity);
            if (value.ToString() == className)
            {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static IntPtr FindOwnedWindow(IntPtr owner)
    {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter)
        {
            IntPtr current = GetWindow(window, 4);
            while (current != IntPtr.Zero)
            {
                if (current == owner)
                {
                    result = window;
                    return false;
                }
                current = GetWindow(current, 4);
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool GetGUIThreadInfo(
        uint threadId, ref GUITHREADINFO info);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(
        IntPtr window, IntPtr processId);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern void keybd_event(
        byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageText(
        IntPtr window, uint message, IntPtr wParam, string lParam);

    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessage(
        IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "PostMessageW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessage(
        IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageGetText(
        IntPtr window, uint message, IntPtr maximum, StringBuilder text);
}
'@

$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'simplefiler-ui-' + [Guid]::NewGuid().ToString('N'))
$targetFolder = Join-Path $testRoot 'jump-target'
$appPath = Join-Path $testRoot 'SimpleFiler.exe'
$appLaunchMarker = Join-Path $testRoot 'aa-launch-marker.txt'
$process = $null

try {
    New-Item -ItemType Directory -Path $targetFolder | Out-Null
    Copy-Item -LiteralPath $Executable -Destination $appPath

    $settings = @{
        schemaVersion = 1
        window = @{
            x = 20
            y = 20
            width = 900
            height = 600
            maximized = $false
        }
        layout = @{
            twoPanes = $true
            splitRatio = 0.5
            sidebarVisible = $true
        }
        panes = @(
            @{
                path = $testRoot
                sortColumn = 0
                sortAscending = $true
                showHidden = $false
            },
            @{
                path = $targetFolder
                sortColumn = 0
                sortAscending = $true
                showHidden = $false
            }
        )
        bookmarks = @(
            @{
                id = 'folder-work'
                name = 'Project Folder'
                path = $targetFolder
                alias = 'work'
                keywords = @('project', 'development')
            }
        )
        links = @(
            @{
                id = 'app-notepad'
                type = 'app'
                name = 'Text Editor'
                target = (Join-Path $env:SystemRoot 'System32\notepad.exe')
                arguments = ''
                workingDirectory = $testRoot
                alias = 'note'
                keywords = @('text', 'document')
            },
            @{
                id = 'app-marker'
                type = 'app'
                name = 'Marker Command'
                target = (Join-Path $env:SystemRoot 'System32\cmd.exe')
                arguments = ('/D /C type nul > "' + $appLaunchMarker + '"')
                workingDirectory = $testRoot
                alias = 'mark'
                keywords = @('test')
                runAsAdministrator = $false
            }
        )
    }
    $settingsJson = $settings | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText(
        (Join-Path $testRoot 'simplefiler.json'),
        $settingsJson,
        [Text.UTF8Encoding]::new($false))

    $process = Start-Process -FilePath $appPath -ArgumentList $testRoot `
        -WindowStyle Minimized -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $mainWindow = [IntPtr]::Zero
    while ($mainWindow -eq [IntPtr]::Zero -and
           [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) {
            throw "SimpleFiler exited during startup with $($process.ExitCode)"
        }
        $mainWindow = $process.MainWindowHandle
    }
    if ($mainWindow -eq [IntPtr]::Zero) {
        throw 'SimpleFiler main window was not created'
    }

    $commandEdit =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 111)
    $suggestions =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 326)
    $leftAddress =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 200)
    $leftList =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 210)
    $rightList =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 211)
    $sidebar =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 220)
    if ($commandEdit -eq [IntPtr]::Zero -or
        $suggestions -eq [IntPtr]::Zero -or
        $leftAddress -eq [IntPtr]::Zero -or
        $leftList -eq [IntPtr]::Zero -or
        $rightList -eq [IntPtr]::Zero) {
        throw 'Required command-palette controls were not created'
    }

    # Cancelling the new-folder prompt must restore focus to the file pane so
    # that the Tab accelerator can still switch panes.
    $navigationDeadline = [DateTime]::UtcNow.AddSeconds(10)
    $initialAddress = [Text.StringBuilder]::new(32768)
    do {
        Start-Sleep -Milliseconds 50
        $initialAddress.Clear() | Out-Null
        [void][SimpleFilerNativeMethods]::SendMessageGetText(
            $leftAddress, 0x000D, [IntPtr]$initialAddress.Capacity,
            $initialAddress)
    } while ($initialAddress.ToString() -ne $testRoot -and
             [DateTime]::UtcNow -lt $navigationDeadline)
    if ($initialAddress.ToString() -ne $testRoot) {
        throw 'Initial pane navigation did not finish'
    }
    [void][SimpleFilerNativeMethods]::ShowWindow($mainWindow, 9)
    [void][SimpleFilerNativeMethods]::SetForegroundWindow($mainWindow)
    Start-Sleep -Milliseconds 100
    [void][SimpleFilerNativeMethods]::SendMessage(
        $mainWindow, 0x0111, [IntPtr]314, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::SendMessage(
        $mainWindow, 0x0111, [IntPtr]314, [IntPtr]::Zero)
    $appThread = [SimpleFilerNativeMethods]::GetWindowThreadProcessId(
        $mainWindow, [IntPtr]::Zero)
    $guiInfo = [SimpleFilerNativeMethods+GUITHREADINFO]::new()
    $guiInfo.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($guiInfo)
    if (![SimpleFilerNativeMethods]::GetGUIThreadInfo(
            $appThread, [ref]$guiInfo) -or
        $guiInfo.hwndFocus -ne $leftList) {
        throw 'Could not focus the left file pane before prompt test'
    }
    [SimpleFilerNativeMethods]::keybd_event(
        0x11, 0, 0, [UIntPtr]::Zero)
    [SimpleFilerNativeMethods]::keybd_event(
        0x4E, 0, 0, [UIntPtr]::Zero)
    [SimpleFilerNativeMethods]::keybd_event(
        0x4E, 0, 2, [UIntPtr]::Zero)
    [SimpleFilerNativeMethods]::keybd_event(
        0x11, 0, 2, [UIntPtr]::Zero)
    $promptDeadline = [DateTime]::UtcNow.AddSeconds(5)
    $prompt = [IntPtr]::Zero
    while ($prompt -eq [IntPtr]::Zero -and
           [DateTime]::UtcNow -lt $promptDeadline) {
        Start-Sleep -Milliseconds 50
        $prompt = [SimpleFilerNativeMethods]::FindThreadWindow(
            $appThread, 'SimpleFiler.PromptWindow')
    }
    if ($prompt -eq [IntPtr]::Zero) {
        throw 'New-folder prompt was not created'
    }
    [SimpleFilerNativeMethods]::keybd_event(
        0x1B, 0, 0, [UIntPtr]::Zero)
    [SimpleFilerNativeMethods]::keybd_event(
        0x1B, 0, 2, [UIntPtr]::Zero)
    $closeDeadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([SimpleFilerNativeMethods]::FindThreadWindow(
               $appThread, 'SimpleFiler.PromptWindow') -ne
           [IntPtr]::Zero -and [DateTime]::UtcNow -lt $closeDeadline) {
        Start-Sleep -Milliseconds 50
    }
    if ([SimpleFilerNativeMethods]::FindThreadWindow(
            $appThread, 'SimpleFiler.PromptWindow') -ne
        [IntPtr]::Zero) {
        throw 'Esc did not close the new-folder prompt'
    }
    $promptFocusDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $guiInfo = [SimpleFilerNativeMethods+GUITHREADINFO]::new()
        $guiInfo.cbSize =
            [Runtime.InteropServices.Marshal]::SizeOf($guiInfo)
        $gotGuiInfo = [SimpleFilerNativeMethods]::GetGUIThreadInfo(
            $appThread, [ref]$guiInfo)
    } while ((!$gotGuiInfo -or $guiInfo.hwndFocus -ne $leftList) -and
             [DateTime]::UtcNow -lt $promptFocusDeadline)
    if (!$gotGuiInfo -or $guiInfo.hwndFocus -ne $leftList) {
        throw 'Esc did not restore focus after cancelling new-folder creation'
    }
    [SimpleFilerNativeMethods]::keybd_event(
        0x09, 0, 0, [UIntPtr]::Zero)
    [SimpleFilerNativeMethods]::keybd_event(
        0x09, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $guiInfo = [SimpleFilerNativeMethods+GUITHREADINFO]::new()
    $guiInfo.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($guiInfo)
    if (![SimpleFilerNativeMethods]::GetGUIThreadInfo(
            $appThread, [ref]$guiInfo) -or
        $guiInfo.hwndFocus -ne $rightList) {
        throw 'Tab did not switch panes after cancelling new-folder creation'
    }
    [void][SimpleFilerNativeMethods]::SendMessage(
        $mainWindow, 0x0111, [IntPtr]314, [IntPtr]::Zero)

    # The Windows property sheet is opened outside SimpleFiler's own prompt
    # loop. Closing it must use the common activation-based focus recovery.
    $itemDeadline = [DateTime]::UtcNow.AddSeconds(5)
    $itemCount = 0
    while ($itemCount -eq 0 -and [DateTime]::UtcNow -lt $itemDeadline) {
        Start-Sleep -Milliseconds 50
        $itemCount = [SimpleFilerNativeMethods]::SendMessage(
            $leftList, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    }
    if ($itemCount -eq 0) {
        throw 'No file-list item was available for the property test'
    }
    [SimpleFilerNativeMethods]::keybd_event(
        0x24, 0, 0, [UIntPtr]::Zero)
    [SimpleFilerNativeMethods]::keybd_event(
        0x24, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $selectedItem = [SimpleFilerNativeMethods]::SendMessage(
        $leftList, 0x100C, [IntPtr](-1), [IntPtr]2).ToInt32()
    if ($selectedItem -lt 0) {
        throw 'Could not select a file-list item for the property test'
    }
    [void][SimpleFilerNativeMethods]::PostMessage(
        $mainWindow, 0x0111, [IntPtr]307, [IntPtr]::Zero)
    $propertyDeadline = [DateTime]::UtcNow.AddSeconds(10)
    $propertyWindow = [IntPtr]::Zero
    while ($propertyWindow -eq [IntPtr]::Zero -and
           [DateTime]::UtcNow -lt $propertyDeadline) {
        Start-Sleep -Milliseconds 50
        $propertyWindow =
            [SimpleFilerNativeMethods]::FindOwnedWindow($mainWindow)
    }
    if ($propertyWindow -eq [IntPtr]::Zero) {
        throw 'Windows property sheet was not created'
    }
    [void][SimpleFilerNativeMethods]::PostMessage(
        $propertyWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $propertyCloseDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([SimpleFilerNativeMethods]::IsWindow($propertyWindow) -and
           [DateTime]::UtcNow -lt $propertyCloseDeadline) {
        Start-Sleep -Milliseconds 50
    }
    if ([SimpleFilerNativeMethods]::IsWindow($propertyWindow)) {
        throw 'Windows property sheet did not close'
    }
    $focusDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 50
        $guiInfo = [SimpleFilerNativeMethods+GUITHREADINFO]::new()
        $guiInfo.cbSize =
            [Runtime.InteropServices.Marshal]::SizeOf($guiInfo)
        $gotGuiInfo = [SimpleFilerNativeMethods]::GetGUIThreadInfo(
            $appThread, [ref]$guiInfo)
    } while ((!$gotGuiInfo -or $guiInfo.hwndFocus -ne $leftList) -and
             [DateTime]::UtcNow -lt $focusDeadline)
    if (!$gotGuiInfo -or $guiInfo.hwndFocus -ne $leftList) {
        throw 'Closing the property sheet did not restore focus'
    }
    [void][SimpleFilerNativeMethods]::ShowWindow($mainWindow, 6)

    foreach ($removedButtonId in 100..103) {
        if ([SimpleFilerNativeMethods]::GetDlgItem(
                $mainWindow, $removedButtonId) -ne [IntPtr]::Zero) {
            throw "Removed navigation button $removedButtonId still exists"
        }
    }

    $listHeader = [SimpleFilerNativeMethods]::SendMessage(
        $leftList, 0x101F, [IntPtr]::Zero, [IntPtr]::Zero)
    $columnCount = [SimpleFilerNativeMethods]::SendMessage(
        $listHeader, 0x1200, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($columnCount -ne 3) {
        throw "File list has $columnCount columns instead of 3"
    }

    # A single f/a remains list input. Completing ff/aa moves the full prefix
    # to the command field.
    [void][SimpleFilerNativeMethods]::SendMessage(
        $mainWindow, 0x0111, [IntPtr]314, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::PostMessage(
        $rightList, 0x0102, [IntPtr][char]'f', [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $directInput = [Text.StringBuilder]::new(16)
    [void][SimpleFilerNativeMethods]::SendMessageGetText(
        $commandEdit, 0x000D, [IntPtr]$directInput.Capacity, $directInput)
    if ($directInput.Length -ne 0) {
        throw 'A single f unexpectedly focused command input'
    }
    [void][SimpleFilerNativeMethods]::PostMessage(
        $rightList, 0x0102, [IntPtr][char]'f', [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [void][SimpleFilerNativeMethods]::SendMessageGetText(
        $commandEdit, 0x000D, [IntPtr]$directInput.Capacity, $directInput)
    if ($directInput.ToString() -ne 'ff') {
        throw 'Typing ff in the file list did not focus command input'
    }
    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, '')
    [void][SimpleFilerNativeMethods]::SendMessage(
        $mainWindow, 0x0111, [IntPtr]314, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::PostMessage(
        $leftList, 0x0102, [IntPtr][char]'a', [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $directInput.Clear() | Out-Null
    [void][SimpleFilerNativeMethods]::SendMessageGetText(
        $commandEdit, 0x000D, [IntPtr]$directInput.Capacity, $directInput)
    if ($directInput.Length -ne 0) {
        throw 'A single a unexpectedly focused command input'
    }
    [void][SimpleFilerNativeMethods]::PostMessage(
        $leftList, 0x0102, [IntPtr][char]'a', [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [void][SimpleFilerNativeMethods]::SendMessageGetText(
        $commandEdit, 0x000D, [IntPtr]$directInput.Capacity, $directInput)
    if ($directInput.ToString() -ne 'aa') {
        throw 'Typing aa in the file list did not focus command input'
    }
    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, '')

    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, 'ffwork')
    $folderCount = [SimpleFilerNativeMethods]::SendMessage(
        $suggestions, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($folderCount -lt 1) {
        $sidebarCount = [SimpleFilerNativeMethods]::SendMessage(
            $sidebar, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
        throw "ffwork did not produce a folder candidate " +
            "(sidebar count: $sidebarCount)"
    }

    # Accept through the suggestion list's keyboard handling.
    [void][SimpleFilerNativeMethods]::PostMessage(
        $suggestions, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::PostMessage(
        $suggestions, 0x0101, [IntPtr]0x0D, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    $addressText = [Text.StringBuilder]::new(32768)
    [void][SimpleFilerNativeMethods]::SendMessageGetText(
        $leftAddress, 0x000D, [IntPtr]$addressText.Capacity, $addressText)
    if ($addressText.ToString() -ne $targetFolder) {
        throw "ffwork navigated to '$addressText' instead of '$targetFolder'"
    }

    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, 'aanote')
    $appCount = [SimpleFilerNativeMethods]::SendMessage(
        $suggestions, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($appCount -lt 1) {
        throw 'aanote did not produce an application candidate'
    }

    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, 'aamark')
    $markerCount = [SimpleFilerNativeMethods]::SendMessage(
        $suggestions, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($markerCount -ne 1) {
        throw "aamark produced $markerCount candidates instead of one"
    }
    [void][SimpleFilerNativeMethods]::PostMessage(
        $suggestions, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::PostMessage(
        $suggestions, 0x0101, [IntPtr]0x0D, [IntPtr]::Zero)
    $launchDeadline = [DateTime]::UtcNow.AddSeconds(5)
    while (!(Test-Path -LiteralPath $appLaunchMarker) -and
           [DateTime]::UtcNow -lt $launchDeadline) {
        Start-Sleep -Milliseconds 100
    }
    if (!(Test-Path -LiteralPath $appLaunchMarker)) {
        throw 'aamark candidate did not launch its registered command'
    }

    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, 'cmd admin')
    $terminalCount = [SimpleFilerNativeMethods]::SendMessage(
        $suggestions, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    if ($terminalCount -ne 1) {
        throw "cmd admin produced $terminalCount candidates instead of one"
    }

    # Delete must edit text while the command field has focus. It must not be
    # translated into the file-delete accelerator.
    [void][SimpleFilerNativeMethods]::SendMessageText(
        $commandEdit, 0x000C, [IntPtr]::Zero, 'delete-check')
    [void][SimpleFilerNativeMethods]::SendMessage(
        $mainWindow, 0x0111, [IntPtr]313, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::SendMessage(
        $commandEdit, 0x00B1, [IntPtr]::Zero, [IntPtr]1)
    [void][SimpleFilerNativeMethods]::PostMessage(
        $commandEdit, 0x0100, [IntPtr]0x2E, [IntPtr]::Zero)
    [void][SimpleFilerNativeMethods]::PostMessage(
        $commandEdit, 0x0101, [IntPtr]0x2E, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $editedText = [Text.StringBuilder]::new(128)
    [void][SimpleFilerNativeMethods]::SendMessageGetText(
        $commandEdit, 0x000D, [IntPtr]$editedText.Capacity, $editedText)
    if ($editedText.ToString() -ne 'elete-check') {
        throw 'Delete was not handled as text editing in the command field'
    }

    Write-Output 'SimpleFiler UI smoke tests passed'
}
finally {
    if ($process -ne $null -and !$process.HasExited) {
        $mainWindow = $process.MainWindowHandle
        if ($mainWindow -ne [IntPtr]::Zero) {
            [void][SimpleFilerNativeMethods]::PostMessage(
                $mainWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
            [void]$process.WaitForExit(3000)
        }
        if (!$process.HasExited) {
            Stop-Process -Id $process.Id -Force
            [void]$process.WaitForExit(5000)
        }
    }

    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedTestRoot.StartsWith(
            $resolvedTempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        for ($attempt = 0; $attempt -lt 25; $attempt++) {
            try {
                Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
                break
            }
            catch {
                if ($attempt -eq 24) {
                    throw
                }
                Start-Sleep -Milliseconds 200
            }
        }
    }
}
