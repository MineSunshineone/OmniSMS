[CmdletBinding()]
param(
    [ValidateSet('build', 'flash', 'monitor', 'reconfigure', 'clean', 'fullclean')]
    [string]$Action = 'build',
    [string]$Port = 'COM5',
    [string]$IdfPath = $env:IDF_PATH,
    [string]$IdfToolsPath = $env:IDF_TOOLS_PATH,
    [ValidateSet('esp32c3', 'esp32s3', 'esp32')]
    [string]$Target = 'esp32c3',
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
$PortRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$RepoRoot = (Resolve-Path (Join-Path $PortRoot '..\..')).Path
$BuildName = if ($Target -eq 'esp32c3') { 'esp-idf' } else { "esp-idf-$Target" }
$BuildDir = Join-Path $RepoRoot ("build\" + $BuildName)
$SdkConfig = Join-Path $BuildDir 'sdkconfig'

if ([string]::IsNullOrWhiteSpace($IdfPath)) { $IdfPath = 'E:\Espressif\esp-idf-v5.5.4' }
if ([string]::IsNullOrWhiteSpace($IdfToolsPath)) { $IdfToolsPath = 'E:\Espressif\.espressif' }

$ExportScript = Join-Path $IdfPath 'export.ps1'
if (-not (Test-Path -LiteralPath $ExportScript)) {
    throw "ESP-IDF export script not found: $ExportScript"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$env:IDF_TOOLS_PATH = $IdfToolsPath
. $ExportScript

$IdfArgs = @('-B', $BuildDir, '-D', "SDKCONFIG=$SdkConfig")
function Set-OmniSmsTarget {
    $expected = 'CONFIG_IDF_TARGET="' + $Target + '"'
    $matches = Test-Path -LiteralPath $SdkConfig
    if ($matches) {
        $matches = Select-String -LiteralPath $SdkConfig -SimpleMatch $expected -Quiet
    }
    if (-not $matches) {
        idf.py @IdfArgs set-target $Target
        if ($LASTEXITCODE -ne 0) { throw "Failed to configure ESP-IDF target: $Target" }
    }
}

Push-Location $PortRoot
try {
    switch ($Action) {
        'build' {
            Set-OmniSmsTarget
            if ($Jobs -le 0) {
                $Jobs = [int]$env:NUMBER_OF_PROCESSORS
                if ($Jobs -le 0) { $Jobs = 4 }
            }
            $previousNinjaFlags = $env:NINJAFLAGS
            $env:NINJAFLAGS = "-j$Jobs"
            try {
                idf.py @IdfArgs build
            } finally {
                $env:NINJAFLAGS = $previousNinjaFlags
            }
        }
        'flash' { Set-OmniSmsTarget; idf.py @IdfArgs -p $Port flash }
        'monitor' { idf.py @IdfArgs -p $Port monitor }
        'reconfigure' { Set-OmniSmsTarget; idf.py @IdfArgs reconfigure }
        'clean' { idf.py @IdfArgs clean }
        'fullclean' { idf.py @IdfArgs fullclean }
    }
} finally {
    Pop-Location
}
