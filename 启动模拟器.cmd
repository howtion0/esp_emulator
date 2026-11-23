@echo off
setlocal
cd /d "%~dp0desktop"
if not exist node_modules\.bin\electron.cmd call npm.cmd ci
if errorlevel 1 exit /b 1
call npm.cmd start
