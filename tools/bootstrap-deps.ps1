Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo_root = Split-Path -Parent $PSScriptRoot

Push-Location $repo_root
try {
    git submodule sync --recursive
    git submodule update --init --recursive

    Write-Host 'Pinned dependency revisions:'
    git -C external/SDL rev-parse HEAD
    git -C external/entt rev-parse HEAD
    git -C external/bgfx.cmake rev-parse HEAD
    git -C external/bgfx.cmake/bgfx rev-parse HEAD
    git -C external/bgfx.cmake/bimg rev-parse HEAD
    git -C external/bgfx.cmake/bx rev-parse HEAD
}
finally {
    Pop-Location
}