# Compila smartylens.c in SmartyLens.exe.
# Serve solo Python: al primo avvio crea un ambiente isolato e vi installa il compilatore Zig (pacchetto PyPI "ziglang").
param([string]$VenvDir = "$PSScriptRoot\.build-venv")
$ErrorActionPreference = 'Stop'
$py = "$VenvDir\Scripts\python.exe"
if (-not (Test-Path $py)) {
    python -m venv $VenvDir
    & $py -m pip install --quiet --disable-pip-version-check ziglang
}
& $py -m ziglang cc -target x86_64-windows-gnu -Os -s -municode "-Wl,--subsystem,windows" `
    "$PSScriptRoot\smartylens.c" -o "$PSScriptRoot\SmartyLens.exe" `
    -lwinhttp -lgdi32 -luser32 -lshell32 -ladvapi32 -ldwmapi -luxtheme
if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita" }
Remove-Item "$PSScriptRoot\SmartyLens.pdb" -ErrorAction SilentlyContinue
"OK: $PSScriptRoot\SmartyLens.exe ($([math]::Round((Get-Item "$PSScriptRoot\SmartyLens.exe").Length / 1KB)) KB)"
