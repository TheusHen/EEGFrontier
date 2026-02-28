$ErrorActionPreference = "Stop"

# Force Reflex to use npm instead of bun in this environment.
$env:REFLEX_USE_NPM = "1"

# Ensure a valid npm path appears before the broken System32 shim.
$nodeBin = "C:\Program Files\nodejs"
if (Test-Path $nodeBin) {
  $env:PATH = "$nodeBin;$env:PATH"
}

Write-Host "Starting Reflex with npm..."
Write-Host "REFLEX_USE_NPM=$env:REFLEX_USE_NPM"

& .\.venv\Scripts\reflex run
