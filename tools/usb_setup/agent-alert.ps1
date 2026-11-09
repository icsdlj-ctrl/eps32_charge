param(
    [Parameter(Position = 0)][string]$Event = 'off',
    [Parameter(Position = 1)][string]$Source = 'cursor'
)
$ErrorActionPreference = 'Stop'
try {
    if ([Console]::IsInputRedirected) { $null = [Console]::In.ReadToEnd() }
    $dir = Join-Path $env:LOCALAPPDATA 'HoloStation'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $path = Join-Path $dir 'alert.json'
    $mode = 'off'
    if ($Event -eq 'confirm') { $mode = 'confirm' }
    elseif ($Event -eq 'done') { $mode = 'done' }
    elseif ($Event -eq 'clear' -and (Test-Path -LiteralPath $path)) {
        $cur = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
        if ([string]$cur.mode -eq 'done') { exit 0 }
    }
    $payload = @{ mode = $mode; source = $Source; at = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($path, ($payload | ConvertTo-Json -Compress), $utf8)
    if ($Event -eq 'prompt') { [Console]::Out.Write('{"continue":true}') }
} catch {
    if ($Event -eq 'prompt') { [Console]::Out.Write('{"continue":true}') }
}
if ($Event -eq 'confirm') { exit 1 }
exit 0
