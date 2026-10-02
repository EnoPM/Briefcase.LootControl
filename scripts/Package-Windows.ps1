[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [string]$LicensePath = ''
)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version = (Get-Content -LiteralPath (Join-Path $project 'VERSION') -Raw).Trim()
if ($version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'VERSION must contain MAJOR.MINOR.PATCH.'
}
$dll = [IO.Path]::GetFullPath($DllPath)
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw "Missing compiled mod: $dll" }
if (-not $LicensePath) {
    $LicensePath = Join-Path $project '..\..\build-ue4ss-ninja\_deps\nlohmann_json-src\LICENSE.MIT'
}
$license = [IO.Path]::GetFullPath($LicensePath)
if (-not (Test-Path -LiteralPath $license -PathType Leaf)) { throw "Missing JSON license: $license" }

$stage = [IO.Path]::GetFullPath((Join-Path $project ('build\package-' + [guid]::NewGuid().ToString('N'))))
$dist = [IO.Path]::GetFullPath((Join-Path $project 'dist'))
if (-not $stage.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    -not $dist.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Unsafe package paths.'
}
$archive = Join-Path $dist "Briefcase.LootControl-windows-x64-$version.zip"
$mod = Join-Path $stage 'ue4ss\Mods\BriefcaseLootControl'
try {
    foreach ($relative in @('dlls','Data','Licenses')) {
        New-Item -ItemType Directory -Path (Join-Path $mod $relative) -Force | Out-Null
    }
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath $dll -Destination (Join-Path $mod 'dlls\main.dll')
    Copy-Item -LiteralPath (Join-Path $project 'Data\config.json') -Destination (Join-Path $mod 'Data\config.json')
    Copy-Item -LiteralPath $license -Destination (Join-Path $mod 'Licenses\nlohmann-json.txt')
    Copy-Item -LiteralPath (Join-Path $project 'README.md') -Destination (Join-Path $mod 'README.md')
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive)
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try { $entries = @($zip.Entries | ForEach-Object FullName) }
    finally { $zip.Dispose() }
    $expected = @(
        'ue4ss/Mods/BriefcaseLootControl/dlls/main.dll',
        'ue4ss/Mods/BriefcaseLootControl/Data/config.json',
        'ue4ss/Mods/BriefcaseLootControl/Licenses/nlohmann-json.txt',
        'ue4ss/Mods/BriefcaseLootControl/README.md'
    )
    foreach ($item in $expected) { if ($item -cnotin $entries) { throw "Package is missing $item" } }
    if (@($entries | Where-Object { $_ -match '\.pdb$' }).Count) { throw 'Package contains debug symbols.' }
    Write-Output "Packaged $archive"
} finally {
    if ($stage.StartsWith($project + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $stage)) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
