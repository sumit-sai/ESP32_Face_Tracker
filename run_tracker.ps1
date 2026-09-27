# Forward all arguments to Python, for example: .\run_tracker.ps1 --dry-run
$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot 'pc_tracker/.venv/Scripts/python.exe'
$entry = Join-Path $PSScriptRoot 'pc_tracker/track.py'
$nvidia = Join-Path $PSScriptRoot 'pc_tracker/.venv/Lib/site-packages/nvidia'
if (-not (Test-Path -LiteralPath $python)) {
    throw 'Create pc_tracker/.venv and install requirements first; see README.md.'
}
$previousPath = $env:PATH
$trackerExitCode = 1
try {
    if (Test-Path -LiteralPath $nvidia) {
        $dllDirectories = @(Get-ChildItem -LiteralPath $nvidia -Recurse -Filter '*.dll' -File |
            Select-Object -ExpandProperty DirectoryName -Unique)
        if ($dllDirectories.Count -gt 0) {
            $env:PATH = ($dllDirectories -join [IO.Path]::PathSeparator) + [IO.Path]::PathSeparator + $previousPath
        }
    }
    & $python $entry @args
    $trackerExitCode = $LASTEXITCODE
}
finally {
    $env:PATH = $previousPath
}
exit $trackerExitCode
