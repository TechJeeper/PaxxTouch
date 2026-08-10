param(
    [string]$PrinterHost = "192.168.70.100"
)

function Test-Url($label, $url) {
    try {
        $r = Invoke-WebRequest -Uri $url -TimeoutSec 8 -UseBasicParsing
        Write-Host "[OK] $label HTTP $($r.StatusCode) len=$($r.RawContentLength)"
        return $true
    } catch {
        Write-Host "[FAIL] $label $($_.Exception.Message)"
        return $false
    }
}

Write-Host "Testing printer at $PrinterHost"
Test-Url "Moonraker info" "http://${PrinterHost}:7125/server/info"
Test-Url "Remote /screen/" "http://${PrinterHost}/screen/"
Test-Url "Remote snapshot" "http://${PrinterHost}/screen/snapshot"
Test-Url "Camera snapshot" "http://${PrinterHost}/webcam/snapshot.jpg"
try {
    $j = Invoke-RestMethod -Uri "http://${PrinterHost}:7125/printer/objects/query?print_task_config" -TimeoutSec 8
    $cfg = $j.result.status.print_task_config
    Write-Host "[OK] print_task_config filament_exist=$($cfg.filament_exist -join ',')"
} catch {
    Write-Host "[FAIL] print_task_config $($_.Exception.Message)"
}
try {
    $j = Invoke-RestMethod -Uri "http://${PrinterHost}:7125/server/files/list?root=gcodes" -TimeoutSec 8
    Write-Host "[OK] files list count=$($j.result.Count)"
} catch {
    Write-Host "[FAIL] files list $($_.Exception.Message)"
}
