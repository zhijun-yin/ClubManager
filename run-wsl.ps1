param(
    [ValidateSet('setup','build','clean','test','init','reset','run','demo')]
    [string]$Action = 'run',
    [string]$Distro = 'Ubuntu-24.04',
    [string]$Username = 'admin',
    [ValidateRange(1,65535)][int]$Port = 8090
)
$ErrorActionPreference = 'Stop'
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL is not installed. See README.md for supported Linux setup.'
}
# --cd handles the Windows project path, including Chinese characters and spaces.
# Arguments are passed separately; usernames and paths are never evaluated as shell code.
& wsl.exe -d $Distro --cd $PSScriptRoot --exec bash scripts/run-linux.sh $Action $Username $Port
if ($LASTEXITCODE -ne 0) { throw "Linux action '$Action' failed (exit $LASTEXITCODE)." }
