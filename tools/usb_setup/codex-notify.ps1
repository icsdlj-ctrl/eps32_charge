$ErrorActionPreference = 'Continue'
$alert = Join-Path $PSScriptRoot 'agent-alert.ps1'
$blob = [string]($args -join ' ')
$event = if ($blob -match 'approval|elicitation|permission') { 'confirm' } else { 'done' }
Start-Process -FilePath powershell.exe -WindowStyle Hidden -Wait -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $alert, $event, 'codex'
) | Out-Null
$orig = 'C:\Users\dlj2\AppData\Local\OpenAI\Codex\runtimes\cua_node\cfb32733c877621e\bin\node_modules\@oai\sky\bin\windows\codex-computer-use.exe'
if (Test-Path -LiteralPath $orig) {
    & $orig 'turn-ended' @args
    exit $LASTEXITCODE
}
exit 0
