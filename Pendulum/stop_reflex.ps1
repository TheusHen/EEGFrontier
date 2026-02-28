$ErrorActionPreference = "SilentlyContinue"

$ports = 3000, 8000, 8001
$pids = @()

foreach ($port in $ports) {
  $lines = netstat -ano | Select-String ":$port"
  foreach ($line in $lines) {
    $lineText = $line.ToString()
    if ($lineText -notmatch "LISTENING") {
      continue
    }
    $parts = ($lineText -replace "\s+", " ").Trim().Split(" ")
    if ($parts.Length -gt 0) {
      $pidToken = $parts[-1]
      if ($pidToken -match "^\d+$") {
        $pidInt = [int]$pidToken
        if ($pidInt -ne $PID) {
          $pids += $pidInt
        }
      }
    }
  }
}

$pids = $pids | Sort-Object -Unique
if (-not $pids -or $pids.Count -eq 0) {
  Write-Host "No Reflex-related processes found on ports 3000/8000/8001."
  exit 0
}

Write-Host "Stopping PIDs: $($pids -join ', ')"
foreach ($procId in $pids) {
  try {
    Stop-Process -Id $procId -Force -ErrorAction Stop
    Write-Host "Stopped $procId"
  } catch {
    Write-Host "Skipped $procId"
  }
}
