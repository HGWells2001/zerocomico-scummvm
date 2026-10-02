@echo off
setlocal
"%~dp0scummvm.exe" zerocomico
if errorlevel 1 (
  echo.
  echo Zero Comico non risulta ancora configurato.
  echo Avvio la procedura di configurazione...
  call "%~dp0Configura-Zero-Comico.cmd"
)
endlocal
