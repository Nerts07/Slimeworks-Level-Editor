$ErrorActionPreference = 'Stop'

$gcc = 'C:\ProgramData\mingw64\mingw64\bin\gcc.exe'
$raylib = Join-Path $PSScriptRoot '..\third_party\raylib'
$output = Join-Path $PSScriptRoot 'Slimeworks-Level-Editor.exe'
$sources = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Filter '*.c' -Recurse | ForEach-Object { $_.FullName }

if (-not (Test-Path -LiteralPath $gcc)) {
    throw "MinGW GCC не найден: $gcc"
}
if (-not (Test-Path -LiteralPath (Join-Path $raylib 'lib\libraylib.a'))) {
    throw "raylib 6.0 не найден: $raylib"
}

$args = @('-Wall', '-Wextra', '-Wpedantic', '-std=c11', '-g', "-I$($PSScriptRoot)\src", "-I$($raylib)\include")
$args += $sources
$args += @('-o', $output, "-L$($raylib)\lib", '-lraylib', '-lopengl32', '-lgdi32', '-lwinmm', '-lm')
& $gcc @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Copy-Item -LiteralPath (Join-Path $raylib 'lib\raylib.dll') -Destination (Split-Path $output) -Force
Write-Host "Built $output"
