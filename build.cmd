@echo off
REM Simple batch wrapper for build.ps1
REM Usage: build.cmd [Configuration] [Platform]
REM   Configuration: Debug or Release (default: Release)
REM   Platform: Win32 or x64 (default: x64)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*