param(
    [Parameter(Mandatory = $true)]
    [string]$QtPrefix
)

$ErrorActionPreference = 'Stop'
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root 'build-windows-x64'
$qtBin = Join-Path $QtPrefix 'bin'
$previousPath = $env:Path

try {
    $env:Path = "$qtBin;$previousPath"
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix" '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & ctest --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }

    $env:Path = $previousPath
    $exe = Join-Path $build 'Release/simplemap.exe'
    $version = $env:ZHILI_RELEASE_VERSION
    if (!$version) {
        $entry = Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$'
        if ($entry) { $version = $entry.Matches[0].Groups[1].Value }
    }
    if ($version -notmatch '^v?[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$') {
        throw 'Cannot determine a valid ZhiLi release version.'
    }
    $version = 'v' + ($version -replace '^v', '')
    $out = Join-Path $build 'dist'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $name = "ZhiLi-$version-Windows-x64.zip"
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vsRoot) { throw 'Visual Studio C++ tools were not found.' }
    $crt = Get-ChildItem -Path (Join-Path $vsRoot 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT') -Directory |
        Sort-Object { [version]$_.Parent.Parent.Name } -Descending | Select-Object -First 1
    if (!$crt) { throw 'The x64 Visual C++ application-local runtime was not found.' }
    & python (Join-Path $root 'packaging/assemble.py') windows `
        --app $exe `
        --entry (Join-Path $build 'Release/ZhiLi.exe') `
        --qt-prefix $QtPrefix `
        --qt-notices (Join-Path $build 'qt-license-notices') `
        --search $crt.FullName `
        --output (Join-Path $out $name)
    if ($LASTEXITCODE -ne 0) { throw 'Assemble failed.' }
    $check = Join-Path $build 'startup-check'
    if (Test-Path -LiteralPath $check) { Remove-Item -LiteralPath $check -Recurse -Force }
    New-Item -ItemType Directory -Path $check | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $out $name), $check)
    $env:QT_QPA_PLATFORM = 'windows'
    $proc = Start-Process -FilePath (Join-Path $check 'ZhiLi.exe') -WorkingDirectory $check -WindowStyle Hidden -PassThru
    try {
        if ($proc.WaitForExit(3000)) { throw "Portable app exited during startup (exit $($proc.ExitCode))." }
    } finally {
        if (!$proc.HasExited) {
            & taskkill.exe /PID $proc.Id /T /F | Out-Null
            $proc.WaitForExit()
        }
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $check -Recurse -Force
    }
    $hash = (Get-FileHash -LiteralPath (Join-Path $out $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name" | Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
    Write-Output (Join-Path $out $name)
} finally {
    $env:Path = $previousPath
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
}
