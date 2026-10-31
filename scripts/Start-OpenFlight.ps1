$ErrorActionPreference = "Stop"

$bashScript = Join-Path $PSScriptRoot "start_openflight.sh"
if (-not (Test-Path -LiteralPath $bashScript)) {
    throw "Launcher script not found: $bashScript"
}

$wslScript = (& wsl.exe -- wslpath -a $bashScript).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslScript)) {
    throw "Could not translate the launcher path for WSL."
}

Write-Host "Starting OpenFlight in WSL..."
Write-Host "The browser will open when F Prime is ready. Press Ctrl+C to stop everything."
& wsl.exe -- bash $wslScript
exit $LASTEXITCODE
