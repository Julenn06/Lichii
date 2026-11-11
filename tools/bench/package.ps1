param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Build = 'E:\lrel',
    [string]$Stage = 'E:\lxtools\stage',
    [string]$Theme = 'colorful_dark'
)
# Monta el paquete de release desde un arbol de compilacion, comprueba la interfaz fuera de
# pantalla (sin ventanas ni teclas) y crea el zip plano que espera el actualizador integrado.
$ErrorActionPreference = 'Stop'
$pkg = "$Stage\pkg"
$shots = "$Stage\preview_$Version"
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force $pkg | Out-Null
Copy-Item "$Build\bin\lichii.exe", "$Build\bin\qt.conf", "$Build\bin\nx2_switches.txt" $pkg
Copy-Item "$Build\bin\Qt6*.dll" $pkg
Copy-Item -Recurse "$Build\bin\plugins" "$pkg\plugins"

# Los simbolos de cada version se guardan aparte (no van en el zip): sin ellos un informe de
# fallo de esa version no se puede traducir a nombres de funcion.
$pdbDir = "$Stage\pdb\$Version"
New-Item -ItemType Directory -Force $pdbDir | Out-Null
if (Test-Path "$Build\bin\lichii.pdb") { Copy-Item "$Build\bin\lichii.pdb" $pdbDir -Force; "pdb: $pdbDir" }

# Vista previa de la interfaz: plugin offscreen temporal y carpeta user desechable.
$offscreen = "$pkg\plugins\platforms\qoffscreen.dll"
Copy-Item 'E:\lxqt\6.8.3\msvc2022_64\plugins\platforms\qoffscreen.dll' $offscreen -Force
New-Item -ItemType Directory -Force "$pkg\user\config" | Out-Null
Set-Content -Encoding utf8 "$pkg\user\config\qt-config.ini" "[UI]`ntheme\default=false`ntheme=$Theme`n"
if (Test-Path $shots) { Remove-Item -Recurse -Force $shots }
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = 'C:\Windows\Fonts'
$env:EDEN_UI_PREVIEW = $shots
$p = Start-Process -FilePath "$pkg\lichii.exe" -WorkingDirectory $pkg -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; 'preview: timeout' }
Remove-Item Env:QT_QPA_PLATFORM, Env:EDEN_UI_PREVIEW, Env:QT_QPA_FONTDIR
$first = Get-Content "$pkg\user\log\eden_log.txt" -TotalCount 1 -ErrorAction SilentlyContinue
"log: $first"
Get-Content "$pkg\user\log\eden_log.txt" -ErrorAction SilentlyContinue |
    Select-String '<Error>|<Critical>' | Select-Object -First 6 |
    ForEach-Object { $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
Remove-Item -Recurse -Force "$pkg\user"
Remove-Item $offscreen -Force
$count = (Get-ChildItem $shots -Filter *.png -ErrorAction SilentlyContinue).Count
"preview images: $count"
if ($count -lt 30) { throw 'la vista previa de la interfaz no genero las imagenes esperadas' }

$zip = "$Stage\Lichii-Frizti-v3-mejorado-v$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip }
python -c "import os,sys,zipfile;z=zipfile.ZipFile(sys.argv[2],'w',zipfile.ZIP_DEFLATED,compresslevel=9);os.chdir(sys.argv[1]);[z.write(os.path.join(r,f),'/'.join(os.path.relpath(os.path.join(r,f),'.').split(os.sep))) for r,_,fs in os.walk('.') for f in fs];z.close()" $pkg $zip
"zip: $zip $([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB"
