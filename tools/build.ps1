param(
    [ValidateSet('build','upload','clean')][string]$Action = 'build',
    [string]$Port = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pythonPath = Join-Path $projectRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) { throw 'Crie .venv e instale platformio primeiro; veja README.md.' }
# A temporary drive alias avoids MAX_PATH failures when unpacking ESP-IDF.
# All files still belong to this project. No project files are moved/deleted.
$buildDrive = $null
foreach ($candidate in @('V:', 'W:', 'X:', 'Y:', 'Z:')) {
    if (-not (Test-Path -LiteralPath ($candidate + '\'))) { $buildDrive = $candidate; break }
}
if (-not $buildDrive) { throw 'Nenhuma letra livre entre V: e Z: para abreviar o caminho.' }
$projectParent = Split-Path -Parent $projectRoot
$projectLeaf = Split-Path -Leaf $projectRoot
$shortProject = $buildDrive + '\' + $projectLeaf
# PlatformIO's relative-path calculation also requires a non-root project path.
& subst.exe $buildDrive $projectParent
if ($LASTEXITCODE -ne 0) { throw 'Nao foi possivel criar alias de caminho com subst.' }
$buildExit = 1
$previousGitCeiling = $env:GIT_CEILING_DIRECTORIES
# PlatformIO configures the bootloader in a separate CMake invocation.
# Keep SDK version detection from walking into an unborn application repo.
$env:GIT_CEILING_DIRECTORIES = $projectRoot + ';' + $shortProject + ';' + $previousGitCeiling
Push-Location $shortProject
try {
    $pioArgs = @('-m', 'platformio', 'run', '-e', 'esp32cam')
    if ($Action -eq 'upload') {
        if (-not $Port) { throw 'Informe -Port COM5 (ou a porta da placa).' }
        $pioArgs += @('-t', 'upload', '--upload-port', $Port)
    }
    if ($Action -eq 'clean') { $pioArgs += @('-t', 'clean') }
    & '.\.venv\Scripts\python.exe' @pioArgs
    $buildExit = $LASTEXITCODE
}
finally {
    Pop-Location
    $env:GIT_CEILING_DIRECTORIES = $previousGitCeiling
    & subst.exe $buildDrive /d
}
exit $buildExit
