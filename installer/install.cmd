@echo off
setlocal

cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
set ec=%ERRORLEVEL%

if not "%ec%"=="0" (
  echo.
  echo [ERROR] install.ps1 failed with exit code %ec%
  echo Press any key to close...
  pause >nul
  exit /b %ec%
)

echo.
echo [OK] Installation completed. Press any key to close...
pause >nul
exit /b 0