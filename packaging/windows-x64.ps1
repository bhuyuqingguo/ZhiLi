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
$stage = Join-Path $build ("stage-" + [Guid]::NewGuid().ToString('N'))

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
    foreach ($library in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Svg.dll')) {
        Copy-Item -LiteralPath (Join-Path $qtBin $library) -Destination $bundle
    }
    "[Paths]`nPrefix=.`nPlugins=.`n[Platforms]`nWindowsArguments=fontengine=freetype" |
        Set-Content -LiteralPath (Join-Path $bundle 'qt.conf') -Encoding ascii
    $plugins = Join-Path $bundle 'platforms'
    New-Item -ItemType Directory -Path $plugins | Out-Null
    Copy-Item -LiteralPath (Join-Path $QtPrefix 'plugins/platforms/qwindows.dll') -Destination $plugins
    # SVGs are used both by the map renderer and by icon/preview widgets. Keep
    # Qt's image and icon engines in the portable bundle, not just Qt5Svg.dll.
    foreach ($plugin in @('iconengines/qsvgicon.dll', 'imageformats/qsvg.dll')) {
        $destination = Join-Path $bundle $plugin
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $QtPrefix "plugins/$plugin") -Destination $destination
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vsRoot) { throw 'Visual Studio C++ tools were not found.' }
    $crt = Get-ChildItem -Path (Join-Path $vsRoot 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT') -Directory |
        Sort-Object { [version]$_.Parent.Parent.Name } -Descending | Select-Object -First 1
    if (!$crt) { throw 'The x64 Visual C++ application-local runtime was not found.' }
    Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $bundle
    foreach ($file in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Svg.dll', 'platforms/qwindows.dll', 'iconengines/qsvgicon.dll', 'imageformats/qsvg.dll', 'vcruntime140.dll', 'msvcp140.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $bundle $file) -PathType Leaf)) {
            throw "Bundle is missing $file"
        }
    }
    Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $bundle
    Copy-Item -LiteralPath (Join-Path $root 'examples') -Recurse -Destination $bundle
    $env:Path = $previousPath
    $env:QT_QPA_PLATFORM = 'windows'
    $proc = Start-Process -FilePath $exe -WorkingDirectory $bundle -WindowStyle Hidden -PassThru
    try {
        if ($proc.WaitForExit(3000)) { throw "Portable app exited during startup (exit $($proc.ExitCode))." }
    } finally {
        if (!$proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    }
    $licenses = Join-Path $bundle 'licenses'
    New-Item -ItemType Directory -Path $licenses | Out-Null
    Copy-Item -Path (Join-Path $root 'assets/fonts/OFL-*.txt') -Destination $licenses
    Copy-Item -LiteralPath (Join-Path $root 'assets/symbols/LICENSE.txt') -Destination (Join-Path $licenses 'SYMBOLS-CC0.txt')
    Copy-Item -LiteralPath (Join-Path $root 'src/vendor/miniz/LICENSE') -Destination (Join-Path $licenses 'MINIZ-MIT.txt')
    $qtNotices = Join-Path $build 'qt-license-notices'
    foreach ($notice in @('LICENSE.LGPL3', 'LICENSE.GPL3')) {
        Copy-Item -LiteralPath (Join-Path $qtNotices $notice) -Destination $licenses
    }
    Copy-Item -LiteralPath (Join-Path $root 'packaging/QT-NOTICE.txt') -Destination $licenses
    $out = Join-Path $build 'dist'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $name = 'ZhiLi-Windows-x64.zip'
    $archive = Join-Path $out $name
    # Use the legacy Chinese code page for entry names on Chinese Windows.
    [Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
    $encoding = [Text.Encoding]::GetEncoding(936, [Text.EncoderFallback]::ExceptionFallback,
                                              [Text.DecoderFallback]::ExceptionFallback)
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory($bundle, $archive,
        [IO.Compression.CompressionLevel]::Optimal, $true, $encoding)
    $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Read, $encoding)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $bundle -Recurse -File) {
            $entry = [IO.Path]::GetRelativePath($stage, $file.FullName).Replace('\', '/')
            if (!$zip.GetEntry($entry)) { throw "ZIP entry did not round-trip: $entry" }
        }
    } finally { $zip.Dispose() }
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
    Write-Output $archive
} finally {
    $env:Path = $previousPath
    if (Test-Path -LiteralPath $stage) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
        $resolvedBuild = (Resolve-Path -LiteralPath $build).Path + [IO.Path]::DirectorySeparatorChar
        if (!$resolvedStage.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Staging directory is outside the build directory: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
