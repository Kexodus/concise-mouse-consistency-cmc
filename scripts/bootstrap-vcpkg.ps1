[CmdletBinding()]
param(
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $repositoryRoot '.vcpkg'
}
$vcpkgPath = [System.IO.Path]::GetFullPath($Destination)
$configurationPath = Join-Path $repositoryRoot 'vcpkg-configuration.json'
$configuration = Get-Content -Raw -LiteralPath $configurationPath | ConvertFrom-Json
$baseline = $configuration.'default-registry'.baseline

if ($baseline -notmatch '^[0-9a-f]{40}$') {
    throw "vcpkg-configuration.json does not contain a pinned 40-character baseline."
}

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

if (Test-Path -LiteralPath $vcpkgPath) {
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgPath '.git'))) {
        throw "The vcpkg destination exists but is not a Git checkout: $vcpkgPath"
    }
} else {
    Invoke-Git clone --filter=blob:none https://github.com/microsoft/vcpkg.git $vcpkgPath
}

Invoke-Git -C $vcpkgPath fetch --depth 1 origin $baseline
Invoke-Git -C $vcpkgPath checkout --detach $baseline

$bootstrap = Join-Path $vcpkgPath 'bootstrap-vcpkg.bat'
& $bootstrap -disableMetrics
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg bootstrap failed with exit code $LASTEXITCODE."
}

Write-Output "vcpkg is ready at $vcpkgPath (baseline $baseline)."
