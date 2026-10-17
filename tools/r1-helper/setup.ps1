param([string]$Python = 'python', [string]$Adb = 'adb', [string]$Serial = 'ingenic_', [switch]$RegisterCodex)
$ErrorActionPreference = 'Stop'
$Adb = (Get-Command $Adb -ErrorAction Stop).Source
$venvPython = Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    & $Python -m venv (Join-Path $PSScriptRoot '.venv')
    if ($LASTEXITCODE) { throw 'Cannot create Python environment' }
}
& $venvPython -m pip install -r (Join-Path $PSScriptRoot 'requirements-lock.txt')
if ($LASTEXITCODE) { throw 'Cannot install helper dependencies' }
$wslPath = (& wsl.exe -e wslpath -u $PSScriptRoot).Trim()
if ($LASTEXITCODE) { throw 'WSL is required to build the MIPS helpers' }
& wsl.exe -e bash "$wslPath/build.sh"
if ($LASTEXITCODE) { throw 'Cannot build native helpers; set SONIX_CC in WSL if needed' }
& $venvPython -m unittest discover -s (Join-Path $PSScriptRoot 'tests') -v
if ($LASTEXITCODE) { throw 'Helper tests failed' }
if ($RegisterCodex) {
    & codex mcp get sonix-r1 --json *> $null
    if ($LASTEXITCODE -eq 0) { throw 'sonix-r1 is already configured; inspect it before replacing' }
    & codex mcp add sonix-r1 --env "SONIX_ADB=$Adb" --env "SONIX_SERIAL=$Serial" -- $venvPython (Join-Path $PSScriptRoot 'server.py')
    if ($LASTEXITCODE) { throw 'Cannot register MCP server' }
}
Write-Output 'Ready. Prepare /tmp helpers explicitly with cli.py prepare or r1_prepare; setup does not touch the player.'
