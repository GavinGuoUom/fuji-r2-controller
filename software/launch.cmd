@echo off
setlocal
set "fuji_python=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%fuji_python%" set "fuji_python=python"
"%fuji_python%" "%~dp0console.py" %*
