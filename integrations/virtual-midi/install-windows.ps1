param(
    [string]$SourceExecutable = (Join-Path $PSScriptRoot 'bin\ULICHPERC MIDI.exe'),
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$componentDir = Join-Path $env:APPDATA 'shroomnoise\ulichpercs\VirtualMidi'
$destination = Join-Path $componentDir 'ULICHPERC MIDI.exe'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$runName = 'ULICHPERC MIDI'

if (-not $Uninstall) {
    $windowsBuild = [int](Get-CimInstance Win32_OperatingSystem).BuildNumber
    if ($windowsBuild -lt 22000) { throw 'This component requires Windows 11. Older Windows versions are not supported.' }
    if (-not (Test-Path -LiteralPath $SourceExecutable -PathType Leaf)) {
        throw 'Helper executable not found. Build and stage the VirtualMidi CMake component first.'
    }
    if ([IO.Path]::GetFullPath($SourceExecutable) -eq [IO.Path]::GetFullPath($destination)) {
        throw 'Run this installer from the distribution folder, not the installed helper folder.'
    }
}

# Stop only this user's installed helper, never a DAW or an unrelated executable.
$currentSession = (Get-Process -Id $PID).SessionId
Get-Process -Name 'ULICHPERC MIDI' -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.SessionId -eq $currentSession -and $_.Path -eq $destination) {
        Stop-Process -Id $_.Id
        $_.WaitForExit(5000) | Out-Null
    }
}
if ($Uninstall) {
    Remove-ItemProperty -Path $runKey -Name $runName -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $destination -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $componentDir 'ready.json') -ErrorAction SilentlyContinue
    Write-Output 'Removed the ULICHPERC MIDI component for this user.'
    exit
}

New-Item -ItemType Directory -Force -Path $componentDir | Out-Null
Copy-Item -LiteralPath $SourceExecutable -Destination $destination -Force
New-Item -Path $runKey -Force | Out-Null
New-ItemProperty -Path $runKey -Name $runName -PropertyType String -Value ('"' + $destination + '"') -Force | Out-Null
Start-Process -FilePath $destination
Write-Output 'Installed ULICHPERC MIDI. Its compatible Windows MIDI Services runtime must also be installed. Enable the required input in your DAW.'
