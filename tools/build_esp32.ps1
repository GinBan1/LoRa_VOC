[CmdletBinding()]
param(
    [string]$IdfPath = $env:IDF_PATH,
    [string]$EspToolsPath = $env:IDF_TOOLS_PATH,
    [string]$ProjectPath = (Join-Path $PSScriptRoot "..\ESP32_Gateway"),
    [string]$GitRoot = "",
    [string]$BuildPath = "",
    [string]$SdkConfigPath = "",
    [string]$SdkConfigDefaults = "",
    [switch]$CheckOnly,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-BuildPath([string]$Value, [string]$BasePath) {
    if ([System.IO.Path]::IsPathRooted($Value)) { return [System.IO.Path]::GetFullPath($Value) }
    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Value))
}

function Find-BuildCommand([string[]]$Names) {
    foreach ($name in $Names) {
        $command = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command) { return $command.Source }
    }
    return $null
}

function Find-BuildPython {
    if (-not [string]::IsNullOrWhiteSpace($env:IDF_PYTHON_ENV_PATH)) {
        $candidate = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "IDF_PYTHON_ENV_PATH has no Scripts/python.exe. Open the matching ESP-IDF terminal or correct this variable."
        }
        return $candidate
    }
    return Find-BuildCommand @("python.exe", "python")
}

$originalEnvironment = @{}
Get-ChildItem Env: | ForEach-Object { $originalEnvironment[$_.Name] = $_.Value }
$locationPushed = $false

try {
    $project = Resolve-BuildPath $ProjectPath (Get-Location).Path
    if (-not (Test-Path -LiteralPath (Join-Path $project "CMakeLists.txt") -PathType Leaf)) {
        throw "ProjectPath must name an ESP-IDF project containing CMakeLists.txt."
    }
    if ([string]::IsNullOrWhiteSpace($IdfPath)) {
        $idfCommand = Find-BuildCommand @("idf.py")
        if ($idfCommand) { $IdfPath = Split-Path -Parent (Split-Path -Parent $idfCommand) }
    }
    if ([string]::IsNullOrWhiteSpace($IdfPath)) {
        throw "ESP-IDF not located. Open its ESP-IDF terminal (export.ps1), or pass -IdfPath <esp-idf directory>."
    }
    $idf = Resolve-BuildPath $IdfPath (Get-Location).Path
    $idfPy = Join-Path $idf "tools\idf.py"
    $idfTools = Join-Path $idf "tools\idf_tools.py"
    foreach ($required in @($idfPy, $idfTools)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Invalid IdfPath: tools/idf.py and tools/idf_tools.py are required."
        }
    }
    $build = if ($BuildPath) { Resolve-BuildPath $BuildPath $project } else { Join-Path $project "build" }
    $sdkconfig = if ($SdkConfigPath) { Resolve-BuildPath $SdkConfigPath $project } else { Join-Path $project "sdkconfig" }
    $defaults = @()
    if ($SdkConfigDefaults) {
        $defaults = @($SdkConfigDefaults.Split(';') | ForEach-Object { Resolve-BuildPath $_ $project })
    }
    $toolsRoot = if ($EspToolsPath) { Resolve-BuildPath $EspToolsPath (Get-Location).Path } else { $null }
    $git = if ($GitRoot) { Resolve-BuildPath $GitRoot (Get-Location).Path } else { $null }
    if ($git -and -not (Test-Path -LiteralPath $git -PathType Container)) {
        throw "GitRoot does not exist. Omit it to use Git from the ESP-IDF terminal PATH."
    }

    $ninja = Find-BuildCommand @("ninja.exe", "ninja")
    $cmake = Find-BuildCommand @("cmake.exe", "cmake")
    $compiler = Find-BuildCommand @("xtensa-esp32-elf-gcc.exe", "xtensa-esp-elf-gcc.exe")
    $cxx = Find-BuildCommand @("xtensa-esp32-elf-g++.exe", "xtensa-esp-elf-g++.exe")
    $gitCommand = Find-BuildCommand @("git.exe", "git")
    $python = Find-BuildPython
    $spacePaths = @(@($project, $idf, $build, $sdkconfig) + $defaults | Where-Object { $_ -match '\s' })
    # Inspection never exports the SDK environment, reads config values or writes a build.
    if ($CheckOnly -or $DryRun) {
        [ordered]@{
            Mode = "plan-only (not a build or dependency-version check)"
            ProjectPath = $project
            IdfPath = $idf
            EspToolsPath = $toolsRoot
            GitRoot = $git
            BuildPath = $build
            SdkConfigPath = $sdkconfig
            SdkConfigDefaults = $defaults
            ToolsInCurrentEnvironment = [ordered]@{ Python = $python; Ninja = $ninja; CMake = $cmake; CCompiler = $compiler; CxxCompiler = $cxx; Git = $gitCommand }
            PathSpaceWarning = ($spacePaths.Count -gt 0)
            Steps = @("Resolve installed ESP-IDF environment", "idf.py reconfigure", "Configure bootloader with absolute Ninja path", "idf.py build")
            Flash = $false
            ConfigValuesRead = $false
        } | ConvertTo-Json -Depth 5
        return
    }
    if ($spacePaths.Count) {
        throw "ESP-IDF build paths must not contain spaces. Move the project/SDK or choose BuildPath/SdkConfigPath/defaults without spaces; CheckOnly only checks argument resolution."
    }
    $env:IDF_PATH = $idf
    if ($toolsRoot) { $env:IDF_TOOLS_PATH = $toolsRoot }
    $env:IDF_TARGET = "esp32"
    if ($git) {
        foreach ($part in @("cmd", "usr\bin", "mingw64\bin")) {
            $candidate = Join-Path $git $part
            if (Test-Path -LiteralPath $candidate -PathType Container) { $env:PATH = "$candidate;$env:PATH" }
        }
    }
    if (-not $python) {
        throw "Python not found. Open the matching ESP-IDF terminal or set IDF_PYTHON_ENV_PATH to its installed Python environment."
    }
    # Official idf_tools.py selects installed versions from this SDK's tools.json.
    # Only export is called, never install/download. Prefer an already active SDK.
    $environmentMatches = $originalEnvironment["IDF_PATH"] -and
        ((Resolve-BuildPath $originalEnvironment["IDF_PATH"] (Get-Location).Path) -eq $idf) -and
        $env:IDF_PYTHON_ENV_PATH -and $ninja -and $cmake -and $compiler -and $cxx -and
        (-not $EspToolsPath -or $originalEnvironment["IDF_TOOLS_PATH"] -eq $toolsRoot)
    if (-not $environmentMatches) {
        $exportLines = & $python $idfTools --idf-path $idf export --format key-value
        if ($LASTEXITCODE -ne 0) {
            throw "ESP-IDF environment export failed. Use its export.ps1/ESP-IDF terminal; install missing dependencies separately with the official installer."
        }
        foreach ($line in $exportLines) {
            if ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
                $key = $Matches[1]
                $value = $Matches[2]
                if ($key -ieq "PATH") { $value = $value.Replace('%PATH%', $env:PATH) }
                [Environment]::SetEnvironmentVariable($key, $value, "Process")
            }
        }
        $python = Find-BuildPython
    }
    $ninja = Find-BuildCommand @("ninja.exe", "ninja")
    $cmake = Find-BuildCommand @("cmake.exe", "cmake")
    $compiler = Find-BuildCommand @("xtensa-esp32-elf-gcc.exe", "xtensa-esp-elf-gcc.exe")
    $cxx = Find-BuildCommand @("xtensa-esp32-elf-g++.exe", "xtensa-esp-elf-g++.exe")
    $gitCommand = Find-BuildCommand @("git.exe", "git")
    if (-not $python -or -not $ninja -or -not $cmake -or -not $compiler -or -not $cxx -or -not $gitCommand) {
        throw "Missing build dependency (Python, Ninja, CMake, Xtensa compiler or Git). Open the matching ESP-IDF terminal after installing official tools."
    }
    # Temporary process-local entries; preserve inherited entries, no global gitconfig.
    $safeDirectories = @((Split-Path -Parent $project), $project, $idf,
        (Join-Path $idf "components\openthread\openthread"))
    $configCount = 0
    if ($env:GIT_CONFIG_COUNT) {
        if (-not [int]::TryParse($env:GIT_CONFIG_COUNT, [ref]$configCount) -or $configCount -lt 0) {
            throw "GIT_CONFIG_COUNT must be a non-negative integer."
        }
    }
    foreach ($directory in $safeDirectories) {
        [Environment]::SetEnvironmentVariable("GIT_CONFIG_KEY_$configCount", "safe.directory", "Process")
        [Environment]::SetEnvironmentVariable("GIT_CONFIG_VALUE_$configCount", ($directory -replace '\\', '/'), "Process")
        $configCount++
    }
    $env:GIT_CONFIG_COUNT = [string]$configCount
    $env:CC = $compiler
    $env:CXX = $cxx
    $env:ASM = $compiler
    $compilerDir = Split-Path -Parent $compiler
    $idfCMakeArgs = @("-B", $build, "-D", "CMAKE_MAKE_PROGRAM=$ninja",
        "-D", "CMAKE_PROGRAM_PATH=$compilerDir", "-D", "CMAKE_C_COMPILER=$compiler",
        "-D", "CMAKE_CXX_COMPILER=$cxx", "-D", "CMAKE_ASM_COMPILER=$compiler")
    if ($SdkConfigPath) { $idfCMakeArgs += @("-D", "SDKCONFIG=$sdkconfig") }
    if ($defaults.Count) { $idfCMakeArgs += @("-D", "SDKCONFIG_DEFAULTS=$($defaults -join ';')") }

    Push-Location -LiteralPath $project
    $locationPushed = $true
    & $python $idfPy @idfCMakeArgs reconfigure
    if ($LASTEXITCODE -ne 0) { throw "ESP32 configure failed with exit code $LASTEXITCODE" }
    # Retain the nested bootloader Windows workaround with discovered tools.
    & $cmake "-DSDKCONFIG=$sdkconfig" "-DIDF_PATH=$idf" "-DIDF_TARGET=esp32" `
        "-DPYTHON_DEPS_CHECKED=1" "-DPYTHON=$python" `
        "-DEXTRA_COMPONENT_DIRS=$idf/components/bootloader" "-DPROJECT_SOURCE_DIR=$project" `
        "-DIGNORE_EXTRA_COMPONENT=" "-DCMAKE_MAKE_PROGRAM=$ninja" "-GNinja" `
        "-S" (Join-Path $idf "components\bootloader\subproject") "-B" (Join-Path $build "bootloader")
    if ($LASTEXITCODE -ne 0) { throw "ESP32 bootloader configure failed with exit code $LASTEXITCODE" }
    & $python $idfPy @idfCMakeArgs build
    if ($LASTEXITCODE -ne 0) { throw "ESP32 build failed with exit code $LASTEXITCODE" }
}
finally {
    if ($locationPushed) { Pop-Location }
    # Do not leave changed environment in a caller's PowerShell session.
    Get-ChildItem Env: | ForEach-Object {
        if (-not $originalEnvironment.ContainsKey($_.Name)) {
            [Environment]::SetEnvironmentVariable($_.Name, $null, "Process")
        }
    }
    foreach ($key in $originalEnvironment.Keys) {
        $currentValue = [Environment]::GetEnvironmentVariable($key, "Process")
        if ($currentValue -cne $originalEnvironment[$key]) {
            [Environment]::SetEnvironmentVariable($key, $originalEnvironment[$key], "Process")
        }
    }
}
