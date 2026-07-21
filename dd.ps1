[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet('run', 'test')]
    [string] $Command
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Find-MSBuild {
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($installationPath) {
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    throw 'MSBuild.exe was not found. Install Visual Studio with the Desktop development with C++ workload.'
}

function Build-Game([string] $Configuration) {
    $msbuild = Find-MSBuild
    & $msbuild (Join-Path $root 'game.sln') /t:Build "/p:Configuration=$Configuration" /p:Platform=Win32
    if ($LASTEXITCODE -ne 0) {
        throw "$Configuration build failed with exit code $LASTEXITCODE."
    }
}

switch ($Command) {
    'run' {
        Build-Game 'Release'
        Start-Process -FilePath (Join-Path $root 'exe\stunt-car-racer-32.exe') -WorkingDirectory $root
    }
    'test' {
        Build-Game 'Debug'
        $testProcess = Start-Process -FilePath (Join-Path $root 'exe\stunt-car-racer-32d.exe') `
            -ArgumentList '-test' -WorkingDirectory $root -NoNewWindow -Wait -PassThru
        exit $testProcess.ExitCode
    }
}