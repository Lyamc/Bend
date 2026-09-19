# Native Windows installer for this Bend 2 fork.
# Requires Bun on PATH (https://bun.sh). Writes bend.cmd next to bun.exe.

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$main = Join-Path $here "bend2\main.ts"
if (-not (Test-Path $main)) {
  throw "bend2\main.ts not found next to install.ps1"
}

$bun = Get-Command bun -ErrorAction SilentlyContinue
if (-not $bun) {
  $fallback = Join-Path $env:USERPROFILE ".bun\bin\bun.exe"
  if (Test-Path $fallback) {
    $env:PATH = "$(Split-Path $fallback);$env:PATH"
    $bun = Get-Command bun
  }
}
if (-not $bun) {
  throw "Bun is not on PATH. Install it from https://bun.sh then re-run install.ps1"
}

$binDir = Split-Path $bun.Source
$cmd = Join-Path $binDir "bend.cmd"
$mainEsc = $main.Replace("%", "%%")
@(
  "@echo off"
  "setlocal"
  "bun `"$mainEsc`" %*"
) | Set-Content -Path $cmd -Encoding ASCII

Write-Host "Installed $cmd"
Write-Host "Make sure $binDir is on PATH, then run: bend --version"
try {
  & bun $main --version
} catch {
  Write-Host "bend --version failed: $_"
}
