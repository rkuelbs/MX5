[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-mingw",
    [int]$WebPort = 8080,
    [int]$DurationSeconds = 0,
    [switch]$NoLogging,
    [switch]$NoBrowser,
    [switch]$NoGui
)

$ErrorActionPreference = "Stop"
if ($WebPort -lt 1 -or $WebPort -gt 65535) {
    throw "WebPort must be from 1 through 65535."
}
if ($DurationSeconds -lt 0) { throw "DurationSeconds cannot be negative." }

$swRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $swRoot "..\..")).Path
$buildRoot = Join-Path $swRoot $BuildDirectory
$dbc = Join-Path $repoRoot "shared\miata.dbc"
$loggingConfig = Join-Path $swRoot "config\logging.json"
$dashConfig = Join-Path $swRoot "config\dash.json"
$ecmConfig = Join-Path $swRoot "config\ecm_simulator.json"
$vnConfig = Join-Path $swRoot "config\vn300_simulator.json"
$logDirectory = Join-Path $swRoot "logs\dev_stack"
$instance = "miata-dev-$PID"
$signalIpc = "$instance-signals"
$vnIpc = "$instance-vn300"
$stopFile = Join-Path ([IO.Path]::GetTempPath()) "$instance.stop"

$executables = @{
    Logger = Join-Path $buildRoot "src\vehicle_loggerd.exe"
    Ecm = Join-Path $buildRoot "src\ecm_can_simulator.exe"
    Vn300 = Join-Path $buildRoot "src\vn300_simulator.exe"
    Wcm = Join-Path $buildRoot "wcm_simulator_ui\wcm_simulator.exe"
    Dash = Join-Path $buildRoot "dash_ui\miata_dash.exe"
    Web = Join-Path $buildRoot "service_web\miata_service_web.exe"
}
foreach ($entry in $executables.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Missing $($entry.Key) executable: $($entry.Value). Build the project first."
    }
}

[IO.Directory]::CreateDirectory($logDirectory) | Out-Null
[IO.File]::Delete($stopFile)
$started = [Collections.Generic.List[object]]::new()

function Quote-ProcessArgument([string]$value) {
    return '"' + $value.Replace('"', '\"') + '"'
}

function Start-StackProcess([string]$name, [string]$file, [string[]]$arguments, [switch]$Visible) {
    $options = @{
        FilePath = $file
        ArgumentList = $arguments
        WorkingDirectory = $repoRoot
        PassThru = $true
    }
    if (-not $Visible) { $options.WindowStyle = "Hidden" }
    $process = Start-Process @options
    Start-Sleep -Milliseconds 250
    if ($process.HasExited) {
        throw "$name exited during startup with code $($process.ExitCode)."
    }
    $started.Add([pscustomobject]@{ Name = $name; Process = $process })
    Write-Host ("Started {0,-8} PID {1}" -f $name, $process.Id)
    return $process
}

try {
    Start-StackProcess "VN300" $executables.Vn300 @(
        "--scenario", (Quote-ProcessArgument $vnConfig),
        "--local-server", (Quote-ProcessArgument $vnIpc)
    ) | Out-Null

    $loggerArguments = @(
        "--dbc", (Quote-ProcessArgument $dbc),
        "--plugin", "virtualcan", "--can-interface", "can0",
        "--vn300-local", (Quote-ProcessArgument $vnIpc),
        "--ipc-name", (Quote-ProcessArgument $signalIpc),
        "--logging-config", (Quote-ProcessArgument $loggingConfig),
        "--log-directory", (Quote-ProcessArgument $logDirectory),
        "--stop-file", (Quote-ProcessArgument $stopFile)
    )
    if ($NoLogging) { $loggerArguments += "--no-log" }
    $logger = Start-StackProcess "Logger" $executables.Logger $loggerArguments

    Start-StackProcess "ECM" $executables.Ecm @(
        "--dbc", (Quote-ProcessArgument $dbc),
        "--scenario", (Quote-ProcessArgument $ecmConfig),
        "--plugin", "virtualcan", "--interface", "can0"
    ) | Out-Null
    if (-not $NoGui) {
        Start-StackProcess "WCM" $executables.Wcm @(
            "--dbc", (Quote-ProcessArgument $dbc),
            "--plugin", "virtualcan", "--interface", "can0"
        ) -Visible | Out-Null
    }
    Start-StackProcess "Web" $executables.Web @(
        "--listen-address", "127.0.0.1", "--port", $WebPort.ToString(),
        "--ipc-name", (Quote-ProcessArgument $signalIpc),
        "--dbc", (Quote-ProcessArgument $dbc),
        "--logging-config", (Quote-ProcessArgument $loggingConfig),
        "--dash-config", (Quote-ProcessArgument $dashConfig),
        "--log-directory", (Quote-ProcessArgument $logDirectory)
    ) | Out-Null
    if (-not $NoGui) {
        Start-StackProcess "Dash" $executables.Dash @(
            "--data-source", "ipc", "--ipc-name", (Quote-ProcessArgument $signalIpc),
            "--dash-config", (Quote-ProcessArgument $dashConfig)
        ) -Visible | Out-Null
    }

    $url = "http://127.0.0.1:$WebPort/"
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            Invoke-RestMethod -Uri ($url + "api/status") -TimeoutSec 1 | Out-Null
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    if (-not $ready) { throw "The service web interface did not become ready at $url" }
    Write-Host ""
    Write-Host "Development stack is running."
    Write-Host "Service page: $url"
    Write-Host "Logs:         $logDirectory"
    Write-Host "Edit the ECM/VN300 JSON files while running to change the scenarios."
    if (-not $NoGui) { Write-Host "Use the WCM window to exercise steering-wheel inputs." }
    Write-Host "Press Ctrl+C here to stop all processes and finalize the MDF log."
    if (-not $NoBrowser) { Start-Process $url | Out-Null }

    $deadline = if ($DurationSeconds -gt 0) { [DateTime]::UtcNow.AddSeconds($DurationSeconds) } else { [DateTime]::MaxValue }
    while (-not $logger.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    if ($logger.HasExited) { throw "Logger exited unexpectedly with code $($logger.ExitCode)." }
}
finally {
    Write-Host "Stopping development stack..."
    [IO.File]::WriteAllText($stopFile, "stop")
    $loggerEntry = $started | Where-Object Name -eq "Logger" | Select-Object -First 1
    if ($null -ne $loggerEntry -and -not $loggerEntry.Process.HasExited) {
        $loggerEntry.Process.WaitForExit(15000) | Out-Null
    }
    foreach ($entry in ($started | Sort-Object { $_.Name -eq "Logger" })) {
        if (-not $entry.Process.HasExited) {
            $entry.Process.CloseMainWindow() | Out-Null
        }
    }
    Start-Sleep -Milliseconds 500
    foreach ($entry in $started) {
        if (-not $entry.Process.HasExited) {
            Stop-Process -Id $entry.Process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    [IO.File]::Delete($stopFile)
}
