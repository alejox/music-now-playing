@echo off
rem Instala el plugin Now Playing en la carpeta de plugins de OBS para todos los usuarios
rem (%ProgramData%\obs-studio\plugins\obs-now-playing), que es la unica que OBS lee en
rem Windows y no pide administrador. Da igual donde se haya descomprimido el zip.
chcp 65001 >nul
setlocal
set "SRC=%~dp0obs-now-playing"
set "PLUGINS=%ProgramData%\obs-studio\plugins"
set "DEST=%PLUGINS%\obs-now-playing"

if not exist "%SRC%\bin\64bit\obs-now-playing.dll" (
  echo No se encuentra la carpeta obs-now-playing junto a este archivo.
  echo Descomprime el zip completo ^(clic derecho, Extraer todo^) y abre Instalar.cmd desde ahi.
  pause
  exit /b 1
)

if /i "%SRC%"=="%DEST%" (
  echo El plugin ya esta en %DEST%. Abre OBS.
  pause
  exit /b 0
)

tasklist /fi "imagename eq obs64.exe" 2>nul | find /i "obs64.exe" >nul
if not errorlevel 1 (
  echo OBS esta abierto: cierralo y vuelve a abrir Instalar.cmd.
  pause
  exit /b 1
)

if exist "%DEST%" rmdir /s /q "%DEST%"
robocopy "%SRC%" "%DEST%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo No se pudo copiar el plugin a %DEST%.
  pause
  exit /b 1
)
del /q "%DEST%\bin\64bit\*.pdb" 2>nul

echo Listo: el plugin quedo en %DEST%.
echo Abre OBS: en Fuentes aparece "Now Playing (musica)" y en Docks, "Musica".
pause
