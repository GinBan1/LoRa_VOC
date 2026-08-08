param(
    [string]$IdfPath = "E:\ESP\v5.5.5\esp-idf",
    [string]$EspToolsPath = "C:\Espressif",
    [string]$ProjectPath = "E:\HOME\LoRaWAN\ESP32_Gateway",
    [string]$GitRoot = "D:\Git\Data\Git"
)

$ErrorActionPreference = "Stop"

$pythonEnv = Join-Path $EspToolsPath "tools\python\v5.5.5\venv"
$python = Join-Path $pythonEnv "Scripts\python.exe"
$idfPy = Join-Path $IdfPath "tools\idf.py"
$ninjaDir = Join-Path $EspToolsPath "tools\ninja\1.12.1"
$cmakeDir = Join-Path $EspToolsPath "tools\cmake\3.30.2\bin"
$toolchainDir = Join-Path $EspToolsPath "tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$romElfRoot = Join-Path $EspToolsPath "tools\esp-rom-elfs"

$requiredPaths = @($IdfPath, $python, $idfPy, $ninjaDir, $cmakeDir, $toolchainDir, $ProjectPath)
foreach ($requiredPath in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "ESP32 build dependency not found: $requiredPath"
    }
}

if ([string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITECTURE)) {
    $env:PROCESSOR_ARCHITECTURE = "AMD64"
}
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $EspToolsPath
$env:IDF_PYTHON_ENV_PATH = $pythonEnv
$env:IDF_TARGET = "esp32"
$gitUsrBin = Join-Path $GitRoot "usr\bin"
$gitMingwBin = Join-Path $GitRoot "mingw64\bin"
$extraPath = @($ninjaDir, $cmakeDir, $toolchainDir, "$pythonEnv\Scripts")
if (Test-Path -LiteralPath $gitUsrBin) {
    $extraPath += $gitUsrBin
}
if (Test-Path -LiteralPath $gitMingwBin) {
    $extraPath += $gitMingwBin
}
$env:PATH = (($extraPath -join ";") + ";" + $env:PATH)

# ESP-IDF uses the ROM ELF files only to generate debugger helper scripts.
# Select the newest installed package automatically so a missing environment
# variable does not produce a misleading warning after an otherwise good build.
$romElfPackage = Get-ChildItem -LiteralPath $romElfRoot -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
if ($null -ne $romElfPackage) {
    $env:ESP_ROM_ELF_DIR = $romElfPackage.FullName
}

# Avoid changing the user's global Git configuration when the ESP-IDF checkout
# and this build process are owned by different Windows accounts.
$safeDirectories = @(
    ((Split-Path -Parent $ProjectPath) -replace "\\", "/"),
    ($ProjectPath -replace "\\", "/"),
    ($IdfPath -replace "\\", "/"),
    ((Join-Path $IdfPath "components\openthread\openthread") -replace "\\", "/")
)
$env:GIT_CONFIG_COUNT = [string]$safeDirectories.Count
for ($index = 0; $index -lt $safeDirectories.Count; $index++) {
    Set-Item -Path "Env:GIT_CONFIG_KEY_$index" -Value "safe.directory"
    Set-Item -Path "Env:GIT_CONFIG_VALUE_$index" -Value $safeDirectories[$index]
}

$ninja = Join-Path $ninjaDir "ninja.exe"
$cCompiler = Join-Path $toolchainDir "xtensa-esp32-elf-gcc.exe"
$cxxCompiler = Join-Path $toolchainDir "xtensa-esp32-elf-g++.exe"
$env:CC = $cCompiler
$env:CXX = $cxxCompiler
$env:ASM = $cCompiler

Push-Location -LiteralPath $ProjectPath
try {
    $idfCMakeArgs = @(
        "-D", "CMAKE_MAKE_PROGRAM=$ninja",
        "-D", "CMAKE_PROGRAM_PATH=$toolchainDir",
        "-D", "CMAKE_C_COMPILER=$cCompiler",
        "-D", "CMAKE_CXX_COMPILER=$cxxCompiler",
        "-D", "CMAKE_ASM_COMPILER=$cCompiler"
    )

    # Configure the top-level project first. ESP-IDF configures the bootloader
    # as a nested CMake project; pre-populating its cache with the absolute
    # Ninja path avoids relying on the caller's PATH on Windows.
    & $python $idfPy @idfCMakeArgs reconfigure
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 configure failed with exit code $LASTEXITCODE"
    }

    $sdkconfig = Join-Path $ProjectPath "sdkconfig"
    $bootloaderSource = Join-Path $IdfPath "components\bootloader\subproject"
    $bootloaderBuild = Join-Path $ProjectPath "build\bootloader"
    & (Join-Path $cmakeDir "cmake.exe") `
        "-DSDKCONFIG=$sdkconfig" `
        "-DIDF_PATH=$IdfPath" `
        "-DIDF_TARGET=esp32" `
        "-DPYTHON_DEPS_CHECKED=1" `
        "-DPYTHON=$python" `
        "-DEXTRA_COMPONENT_DIRS=$IdfPath/components/bootloader" `
        "-DPROJECT_SOURCE_DIR=$ProjectPath" `
        "-DIGNORE_EXTRA_COMPONENT=" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-GNinja" `
        "-S" $bootloaderSource `
        "-B" $bootloaderBuild
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 bootloader configure failed with exit code $LASTEXITCODE"
    }

    & $python $idfPy @idfCMakeArgs build
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
