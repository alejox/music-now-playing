; Instalador de OBS Now Playing para Windows (sin permisos de administrador).
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!define APP "OBS Now Playing"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSNowPlaying"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "Software\OBSNowPlaying" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APP} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1034 "ProductName" "${APP}"
VIAddVersionKey /LANG=1034 "FileDescription" "Instalador de ${APP}"
VIAddVersionKey /LANG=1034 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1034 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1034 "LegalCopyright" "MIT"

!define MUI_ICON "${STAGE}\icon.ico"
!define MUI_UNICON "${STAGE}\icon.ico"
!define MUI_ABORTWARNING
!ifdef PLUGIN
!define MUI_WELCOMEPAGE_TEXT "Muestra en OBS la canción que suena en tu computador: Spotify, YouTube Music, Apple Music y más.$\r$\n$\r$\nInstala la app y el plugin para OBS (fuente Now Playing y dock Música). Mejor con OBS cerrado.$\r$\n$\r$\nNo hace falta instalar nada más ni tener permisos de administrador."
!else
!define MUI_WELCOMEPAGE_TEXT "Muestra en OBS la canción que suena en tu computador: Spotify, YouTube Music, Apple Music y más.$\r$\n$\r$\nNo hace falta instalar nada más ni tener permisos de administrador."
!endif
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir ${APP} ahora"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Spanish"

; Cierra la copia abierta (si hay) para poder reemplazar sus archivos.
!macro StopRunning
  ${If} ${FileExists} "$INSTDIR\node.exe"
    nsExec::Exec '"$INSTDIR\node.exe" "$INSTDIR\app\server.js" --stop'
    Pop $0
    Sleep 700
  ${EndIf}
!macroend

Section "${APP}" SecMain
  SectionIn RO
  !insertmacro StopRunning

  SetOutPath "$INSTDIR"
  File "${STAGE}\node.exe"
  File "${STAGE}\${APP}.exe"
  File "${STAGE}\icon.ico"
  SetOutPath "$INSTDIR\app"
  File "${STAGE}\app\server.js"
  File "${STAGE}\app\overlay.html"
  File "${STAGE}\app\controls.html"
  File "${STAGE}\app\ui.js"
  File "${STAGE}\app\now-playing.user.js"
  SetOutPath "$INSTDIR\app\media"
  File "${STAGE}\app\media\windows.ps1"

  WriteUninstaller "$INSTDIR\Desinstalar.exe"
  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${APP}.exe" "" "$INSTDIR\icon.ico"

  WriteRegStr HKCU "Software\OBSNowPlaying" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\icon.ico"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

!ifdef PLUGIN
; El plugin de OBS (fuente "Now Playing", dock "Música", diseño vertical, actualizaciones…)
; va a la carpeta de plugins de OBS para todos los usuarios, que no pide administrador.
; Queda en su propia carpeta, así después se actualiza solo desde OBS.
Section "Plugin para OBS" SecPlugin
  ReadEnvStr $1 "ProgramData"
  ${If} $1 == ""
    StrCpy $1 "C:\ProgramData"
  ${EndIf}
  StrCpy $1 "$1\obs-studio\plugins\obs-now-playing"

  ; Con OBS abierto el .dll está en uso: no se puede pisar, pero sí renombrar. El plugin
  ; nuevo carga al reiniciar OBS y borra el .old al arrancar.
  Delete "$1\bin\64bit\obs-now-playing.dll.instalador.old"
  ${If} ${FileExists} "$1\bin\64bit\obs-now-playing.dll"
    Rename "$1\bin\64bit\obs-now-playing.dll" "$1\bin\64bit\obs-now-playing.dll.instalador.old"
  ${EndIf}

  ClearErrors
  SetOutPath "$1"
  File /r "${PLUGIN}\*"
  ${If} ${Errors}
    MessageBox MB_ICONEXCLAMATION|MB_OK "No se pudo instalar el plugin en $1.$\r$\n$\r$\nCierra OBS y vuelve a abrir el instalador, o descomprime obs-now-playing-plugin-…-windows-x64.zip en C:\ProgramData\obs-studio\plugins\ (ver README)."
  ${Else}
    WriteRegStr HKCU "Software\OBSNowPlaying" "PluginDir" "$1"
  ${EndIf}
  SetOutPath "$INSTDIR"
SectionEnd
!endif

Section "Abrir al iniciar Windows" SecStartup
  CreateShortcut "$SMSTARTUP\${APP}.lnk" "$INSTDIR\${APP}.exe" "--background" "$INSTDIR\icon.ico"
SectionEnd

Section /o "Acceso directo en el escritorio" SecDesktop
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${APP}.exe" "" "$INSTDIR\icon.ico"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "La app y su panel de configuración."
!ifdef PLUGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecPlugin} "La fuente Now Playing (horizontal o vertical), el dock Música, el canal de música y las actualizaciones automáticas dentro de OBS 31+. Si OBS está abierto, reinícialo."
!endif
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartup} "Arranca en segundo plano al encender el computador, para que el overlay ya esté listo cuando abras OBS."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Agrega un ícono en el escritorio."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  !insertmacro StopRunning
  Delete "$INSTDIR\app\server.js"
  Delete "$INSTDIR\app\overlay.html"
  Delete "$INSTDIR\app\controls.html"
  Delete "$INSTDIR\app\ui.js"
  Delete "$INSTDIR\app\now-playing.user.js"
  Delete "$INSTDIR\app\media\windows.ps1"
  RMDir "$INSTDIR\app\media"
  RMDir "$INSTDIR\app"
  Delete "$INSTDIR\node.exe"
  Delete "$INSTDIR\${APP}.exe"
  Delete "$INSTDIR\icon.ico"
  Delete "$INSTDIR\Desinstalar.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$SMSTARTUP\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  ; El plugin solo se quita si lo instaló este instalador (su carpeta queda en el registro).
  ReadRegStr $1 HKCU "Software\OBSNowPlaying" "PluginDir"
  ${If} $1 != ""
  ${AndIf} ${FileExists} "$1\bin\64bit\obs-now-playing.dll"
    RMDir /r "$1"
  ${EndIf}
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\OBSNowPlaying"
  ; La configuración y la sesión de Spotify quedan en %APPDATA%\OBS Now Playing por si reinstalas.
SectionEnd
