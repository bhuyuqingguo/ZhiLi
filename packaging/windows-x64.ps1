param(
    [Parameter(Mandatory = $true)]
    [string]$QtPrefix
)

$ErrorActionPreference = 'Stop'
$QtPrefix = (Resolve-Path -LiteralPath $QtPrefix).Path
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root 'build-windows-x64'
$qtBin = Join-Path $QtPrefix 'bin'
$deploy = Join-Path $qtBin 'windeployqt.exe'
$previousPath = $env:Path
$stage = Join-Path $build ("stage-" + [Guid]::NewGuid().ToString('N'))

if (!(Test-Path -LiteralPath $deploy -PathType Leaf)) {
    throw "windeployqt.exe is missing from the Qt SDK at $QtPrefix"
}

try {
    $env:Path = "$qtBin;$previousPath"
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix" '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & ctest --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }

    $bundle = Join-Path $stage 'ZhiLi'
    New-Item -ItemType Directory -Path $bundle | Out-Null
    $exe = Join-Path $bundle 'simplemap.exe'
    Copy-Item -LiteralPath (Join-Path $build 'Release/simplemap.exe') -Destination $exe
    & $deploy --release --no-translations --no-compiler-runtime $exe
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }
    foreach ($file in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Svg.dll', 'platforms/qwindows.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $bundle $file) -PathType Leaf)) {
            throw "Bundle is missing $file"
        }
    }
    Copy-Item -LiteralPath (Join-Path $root 'docs/RELEASE.md') -Destination (Join-Path $bundle 'RELEASE.md')
    $licenses = Join-Path $bundle 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    Copy-Item -Path (Join-Path $root 'assets/fonts/OFL-*.txt') -Destination $licenses
    $out = Join-Path $build 'dist'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $name = 'ZhiLi-Windows-x64.zip'
    $archive = Join-Path $out $name
    Compress-Archive -LiteralPath $bundle -DestinationPath $archive -Force
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
    Write-Output $archive
} finally {
    $env:Path = $previousPath
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
