@echo off
setlocal
title Configura Zero Comico per ScummVM
echo.
echo ==========================================
echo   Zero Comico - configurazione ScummVM
echo ==========================================
echo.
echo Inserisci la cartella che contiene:
echo   Zero Comico.exe
echo   Config.gsc
echo   Mp0 ... Mp5
echo   Mpx
echo.
set /p "GAMEPATH=Cartella del gioco: "
set "GAMEPATH=%GAMEPATH:"=%"

if not exist "%GAMEPATH%\Zero Comico.exe" (
  echo.
  echo ERRORE: Zero Comico.exe non trovato.
  pause
  exit /b 1
)
if not exist "%GAMEPATH%\Config.gsc" (
  echo.
  echo ERRORE: Config.gsc non trovato.
  pause
  exit /b 1
)

echo.
echo Registro il gioco in ScummVM...
"%~dp0scummvm.exe" --add --path="%GAMEPATH%"

echo.
echo Avvio Zero Comico...
"%~dp0scummvm.exe" zerocomico
endlocal
