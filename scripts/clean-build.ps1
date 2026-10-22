[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Low')]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$buildPrefix = $buildRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
$relativePaths = @(
    'CMakeFiles','wsl','CMakeCache.txt','CTestTestfile.cmake','Makefile','cmake_install.cmake',
    'baseline_tests.exe','club.exe','club_tests.exe','_test_save.txt','frontend.js','web-ui-check.js',
    'preview/club_preview.exe','preview/server.pid','preview/server.log','preview/server.err',
    'preview/activity-0.csv','preview/prepare-auth.py','wsl-openssl/apt'
)
$targets = @($relativePaths | ForEach-Object { Join-Path $buildRoot $_ })
$sdk = Join-Path $buildRoot 'wsl-openssl'
if (Test-Path -LiteralPath $sdk) {
    $targets += @(Get-ChildItem -LiteralPath $sdk -Directory -Filter 'packages.*' | ForEach-Object FullName)
    $lib = Join-Path $sdk 'usr/lib'
    if (Test-Path -LiteralPath $lib) {
        $targets += @(Get-ChildItem -LiteralPath $lib -File -Filter '*.a' -Recurse | ForEach-Object FullName)
    }
}
$verified = @()
$totalBytes = 0L
foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target)) { continue }
    $item = Get-Item -LiteralPath $target -Force
    $absolute = [System.IO.Path]::GetFullPath($item.FullName)
    if (-not $absolute.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target is outside the project build directory: $absolute"
    }
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        throw "Refusing to recursively clean a link: $absolute"
    }
    $length = if ($item.PSIsContainer) {
        (Get-ChildItem -LiteralPath $absolute -File -Recurse -Force | Measure-Object -Property Length -Sum).Sum
    } else { $item.Length }
    $verified += [pscustomobject]@{Path=$absolute; Bytes=[long]$length}
}
# Verify every path before deleting any. Active WSL binaries are outside this
# directory; the private SDK's shared libraries and headers are retained.
foreach ($item in $verified) {
    if ($PSCmdlet.ShouldProcess($item.Path, 'Remove obsolete project build/cache')) {
        Remove-Item -LiteralPath $item.Path -Recurse -Force
        $totalBytes += $item.Bytes
    }
}
Write-Output ('Removed {0:N1} MiB of obsolete project files.' -f ($totalBytes / 1MB))
