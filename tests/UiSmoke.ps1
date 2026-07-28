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
    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr parent, int id);

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
    $sidebar =
        [SimpleFilerNativeMethods]::GetDlgItem($mainWindow, 220)
    if ($commandEdit -eq [IntPtr]::Zero -or
        $suggestions -eq [IntPtr]::Zero -or
        $leftAddress -eq [IntPtr]::Zero) {
        throw 'Required command-palette controls were not created'
    }

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
