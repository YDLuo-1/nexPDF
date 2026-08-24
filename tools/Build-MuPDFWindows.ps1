param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$MuPDFRoot = (Join-Path $PSScriptRoot "..\third_party\mupdf"),
    [string]$OutputRoot = (Join-Path $PSScriptRoot "..\out\mupdf-windows-x64"),
    [string]$MSBuildPath = ""
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$MuPDFRoot = (Resolve-Path -LiteralPath $MuPDFRoot).Path
$targets = Join-Path $repositoryRoot "cmake\mupdf-windows-slim.targets"

if ([string]::IsNullOrWhiteSpace($MSBuildPath)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installationPath = & $vswhere -latest -products * -version "[17.0,18.0)" -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            $MSBuildPath = Join-Path $installationPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
        }
    }
}

if ([string]::IsNullOrWhiteSpace($MSBuildPath)) {
    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $MSBuildPath = $command.Source
    }
}

if ([string]::IsNullOrWhiteSpace($MSBuildPath) -or -not (Test-Path -LiteralPath $MSBuildPath)) {
    throw "Visual Studio 2022 MSBuild was not found. Install Desktop development with C++ or pass -MSBuildPath."
}

& (Join-Path $PSScriptRoot "Initialize-MuPDFDependencies.ps1") -MuPDFRoot $MuPDFRoot

$solution = Join-Path $MuPDFRoot "platform\win32\mupdf.sln"
# bin2coff is a Win32 host tool even for x64 MuPDF. Building through the solution
# preserves MuPDF's Release|x64 -> Release|Win32 project mapping.
& $MSBuildPath $solution /m /t:libmupdf "/p:Configuration=$Configuration" /p:Platform=x64 /p:PlatformToolset=v143 "/p:ForceImportAfterCppTargets=$targets"
if ($LASTEXITCODE -ne 0) {
    throw "The slim MuPDF build failed."
}

$platformOutput = Join-Path $MuPDFRoot "platform\win32\x64\$Configuration"
$includeOutput = Join-Path $OutputRoot "include"
$libraryOutput = Join-Path $OutputRoot "lib"
New-Item -ItemType Directory -Force -Path $includeOutput | Out-Null
New-Item -ItemType Directory -Force -Path $libraryOutput | Out-Null
Copy-Item -Recurse -Force -Path (Join-Path $MuPDFRoot "include\mupdf") -Destination $includeOutput

$libraries = @("libmupdf.lib", "libthirdparty.lib", "libresources.lib", "libharfbuzz.lib", "libpkcs7.lib")
foreach ($library in $libraries) {
    $source = Join-Path $platformOutput $library
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Expected MuPDF library was not produced: $source"
    }
    Copy-Item -Force -LiteralPath $source -Destination $libraryOutput
}

Write-Host "Slim MuPDF prefix: $OutputRoot"
