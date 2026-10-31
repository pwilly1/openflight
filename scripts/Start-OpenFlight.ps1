$ErrorActionPreference = "Stop"

$bashScript = Join-Path $PSScriptRoot "start_openflight.sh"
if (-not (Test-Path -LiteralPath $bashScript)) {
    throw "Launcher script not found: $bashScript"
}

$wslPathOutput = & wsl.exe --exec wslpath -a $bashScript
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslPathOutput)) {
    throw "Could not translate the launcher path for WSL."
}
$wslScript = $wslPathOutput.Trim()

Write-Host "Starting OpenFlight in WSL..."
Write-Host "The browser will open when F Prime is ready. Press Ctrl+C to stop everything."
& wsl.exe --exec bash $wslScript
exit $LASTEXITCODE
