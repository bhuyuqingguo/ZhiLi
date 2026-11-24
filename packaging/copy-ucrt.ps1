param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$Source = "${env:ProgramFiles(x86)}/Windows Kits/10/Redist/ucrt/DLLs/x64"
)
$ErrorActionPreference = 'Stop'
# Microsoft documents this unversioned SDK directory for downlevel app-local
# deployment. Copy the entire architecture-specific set, not just the DLL
# named by the loader error. Never source these files from System32.
if (!(Test-Path -LiteralPath $Source -PathType Container)) {
    throw "UCRT redistributables were not found: $Source. Install the Windows SDK or supply -Source."
}
$files = @(Get-ChildItem -LiteralPath $Source -Filter '*.dll' -File)
foreach ($required in @('ucrtbase.dll', 'api-ms-win-crt-runtime-l1-1-0.dll',
    'api-ms-win-crt-heap-l1-1-0.dll', 'api-ms-win-crt-string-l1-1-0.dll',
    'api-ms-win-core-libraryloader-l1-1-0.dll')) {
    if ($required -notin $files.Name) { throw "Incomplete UCRT redistributable: $required" }
}
foreach ($file in $files) {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) { throw "Not a PE DLL: $file" }
    $offset = [BitConverter]::ToInt32($bytes, 60)
    if ($offset -lt 0 -or $offset + 6 -gt $bytes.Length -or
        [BitConverter]::ToUInt32($bytes, $offset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x8664) { throw "Not an x64 DLL: $file" }
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
foreach ($file in $files) {
    $target = Join-Path $Destination $file.Name
    Copy-Item -LiteralPath $file.FullName -Destination $target
    if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
        throw "UCRT copy verification failed: $target"
    }
}
Write-Output "Copied and verified $($files.Count) x64 UCRT DLLs."
