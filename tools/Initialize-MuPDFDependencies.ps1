param(
    [string]$MuPDFRoot = (Join-Path $PSScriptRoot "..\third_party\mupdf")
)

$ErrorActionPreference = "Stop"
$MuPDFRoot = (Resolve-Path -LiteralPath $MuPDFRoot).Path

$dependencies = @(
    @{ Path = "thirdparty/jbig2dec"; Url = "https://github.com/ArtifexSoftware/jbig2dec.git" },
    @{ Path = "thirdparty/mujs"; Url = "https://github.com/ArtifexSoftware/mujs.git" },
    @{ Path = "thirdparty/freetype"; Url = "https://github.com/ArtifexSoftware/thirdparty-freetype2.git" },
    @{ Path = "thirdparty/gumbo-parser"; Url = "https://github.com/ArtifexSoftware/thirdparty-gumbo-parser.git" },
    @{ Path = "thirdparty/harfbuzz"; Url = "https://github.com/ArtifexSoftware/thirdparty-harfbuzz.git" },
    @{ Path = "thirdparty/libjpeg"; Url = "https://github.com/ArtifexSoftware/thirdparty-libjpeg.git" },
    @{ Path = "thirdparty/lcms2"; Url = "https://github.com/ArtifexSoftware/thirdparty-lcms2.git" },
    @{ Path = "thirdparty/openjpeg"; Url = "https://github.com/ArtifexSoftware/thirdparty-openjpeg.git" },
    @{ Path = "thirdparty/zlib"; Url = "https://github.com/ArtifexSoftware/thirdparty-zlib.git" },
    @{ Path = "thirdparty/brotli"; Url = "https://github.com/ArtifexSoftware/thirdparty-brotli.git" },
    @{ Path = "thirdparty/cmark-gfm"; Url = "https://github.com/ArtifexSoftware/thirdparty-cmark-gfm.git" }
)

foreach ($dependency in $dependencies) {
    $treeLine = git -c "safe.directory=$MuPDFRoot" -C $MuPDFRoot ls-tree HEAD -- $dependency.Path
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($treeLine)) {
        throw "Unable to resolve the pinned commit for $($dependency.Path)"
    }
    $commit = ($treeLine -split "\s+")[2]
    $destination = Join-Path $MuPDFRoot $dependency.Path
    $gitMetadata = Join-Path $destination ".git"

    if (-not (Test-Path -LiteralPath $gitMetadata)) {
        git -c http.sslBackend=openssl clone --no-checkout $dependency.Url $destination
        if ($LASTEXITCODE -ne 0) {
            throw "Clone failed for $($dependency.Path)"
        }
    }
    git -c "safe.directory=$destination" -C $destination config http.sslBackend openssl
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to configure Git TLS for $($dependency.Path)"
    }
    git -c http.sslBackend=openssl -c "safe.directory=$destination" -C $destination fetch --depth 1 origin $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Fetch failed for $($dependency.Path) at $commit"
    }
    git -c "safe.directory=$destination" -C $destination checkout --detach $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Checkout failed for $($dependency.Path) at $commit"
    }
}

Write-Host "Initialized pinned MuPDF dependencies without OCR, curl, GUI, extraction, or barcode components."
