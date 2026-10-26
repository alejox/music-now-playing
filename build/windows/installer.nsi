; Instalador de Music Now Playing para Windows. La app no pide permisos de administrador;
; el plugin sí (va en la carpeta de OBS, en Archivos de programa): solo para copiarlo.
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

; APP_NAME es el nombre visible. APP_ID es el nombre de siempre en rutas, el .exe y los
; accesos directos: no cambia para que las instalaciones existentes se actualicen encima.
!define APP_NAME "Music Now Playing"
!define APP_ID "OBS Now Playing"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OBSNowPlaying"

Name "${APP_NAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP_ID}"
InstallDirRegKey HKCU "Software\OBSNowPlaying" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1034 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1034 "FileDescription" "Instalador de ${APP_NAME}"
VIAddVersionKey /LANG=1034 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1034 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1034 "LegalCopyright" "MIT"

!define MUI_ICON "${STAGE}\icon.ico"
!define MUI_UNICON "${STAGE}\icon.ico"
!define MUI_ABORTWARNING
!ifdef PLUGIN
!define MUI_WELCOMEPAGE_TEXT "Muestra en OBS la canción que suena en tu computador: Spotify, YouTube Music, Apple Music y más.$\r$\n$\r$\nInstala la app y el plugin para OBS (fuente Now Playing y dock Música). Cierra OBS antes.$\r$\n$\r$\nEl plugin va en la carpeta de OBS: Windows pedirá permiso para copiarlo."
!else
!define MUI_WELCOMEPAGE_TEXT "Muestra en OBS la canción que suena en tu computador: Spotify, YouTube Music, Apple Music y más.$\r$\n$\r$\nNo hace falta instalar nada más ni tener permisos de administrador."
!endif
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_ID}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir ${APP_NAME} ahora"

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

Section "${APP_NAME}" SecMain
  SectionIn RO
  !insertmacro StopRunning

  SetOutPath "$INSTDIR"
  File "${STAGE}\node.exe"
  File "${STAGE}\${APP_ID}.exe"
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
  CreateShortcut "$SMPROGRAMS\${APP_ID}.lnk" "$INSTDIR\${APP_ID}.exe" "" "$INSTDIR\icon.ico"

  WriteRegStr HKCU "Software\OBSNowPlaying" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\icon.ico"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

!ifdef PLUGIN
; El plugin de OBS (fuente "Now Playing", dock "Música", diseño vertical…) va en la
; carpeta de OBS: el .dll en obs-plugins\64bit y los datos en data\obs-plugins\obs-now-playing.
; Esa carpeta es de administrador: los archivos se preparan en $PLUGINSDIR y se copian con
; robocopy, que pide permiso (solo esa copia; el resto del instalador no lo necesita).
Section "Plugin para OBS" SecPlugin
  ; Dónde está OBS: lo dice su instalador en el registro.
  SetRegView 64
  ReadRegStr $1 HKLM "SOFTWARE\OBS Studio" ""
  SetRegView lastused
  ${If} $1 == ""
    StrCpy $1 "$PROGRAMFILES64\obs-studio"
  ${EndIf}
  ${IfNot} ${FileExists} "$1\bin\64bit\obs64.exe"
    MessageBox MB_ICONEXCLAMATION|MB_OK "No encontré OBS en $1.$\r$\n$\r$\nInstala OBS y vuelve a abrir este instalador, o instala el plugin a mano con el zip (ver README)."
    Return
  ${EndIf}

  ; Con OBS abierto el .dll está en uso y no se puede reemplazar.
  ${Do}
    nsExec::ExecToStack 'cmd /c tasklist /fi "imagename eq obs64.exe" | find /i "obs64.exe"'
    Pop $0
    Pop $2
    ${If} $0 != 0
      ${Break}
    ${EndIf}
    MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL "OBS está abierto: ciérralo y pulsa Reintentar para instalar el plugin." IDRETRY obs_retry
    Return
    obs_retry:
  ${Loop}

  SetOutPath "$PLUGINSDIR\plugin"
  File /r "${PLUGIN}\*"
  SetOutPath "$INSTDIR"
  ExecShellWait "runas" "robocopy.exe" '"$PLUGINSDIR\plugin" "$1" /E /NFL /NDL /NJH /NJS /NP' SW_HIDE

  ${If} ${FileExists} "$1\data\obs-plugins\obs-now-playing\overlay\index.html"
  ${AndIf} ${FileExists} "$1\obs-plugins\64bit\obs-now-playing.dll"
    WriteRegStr HKCU "Software\OBSNowPlaying" "PluginDir" "$1"
    ; Una copia de antes en ProgramData se cargaría junto a esta: se quita.
    ReadEnvStr $2 "ProgramData"
    ${If} $2 != ""
    ${AndIf} ${FileExists} "$2\obs-studio\plugins\obs-now-playing\bin\64bit\obs-now-playing.dll"
      RMDir /r "$2\obs-studio\plugins\obs-now-playing"
    ${EndIf}
  ${Else}
    MessageBox MB_ICONEXCLAMATION|MB_OK "No se pudo copiar el plugin a $1 (¿se canceló el permiso?).$\r$\n$\r$\nVuelve a abrir el instalador, o instala el plugin a mano con el zip (ver README)."
  ${EndIf}
SectionEnd
!endif

Section "Abrir al iniciar Windows" SecStartup
  CreateShortcut "$SMSTARTUP\${APP_ID}.lnk" "$INSTDIR\${APP_ID}.exe" "--background" "$INSTDIR\icon.ico"
SectionEnd

Section /o "Acceso directo en el escritorio" SecDesktop
  CreateShortcut "$DESKTOP\${APP_ID}.lnk" "$INSTDIR\${APP_ID}.exe" "" "$INSTDIR\icon.ico"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "La app y su panel de configuración."
!ifdef PLUGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecPlugin} "La fuente Now Playing (horizontal o vertical), el dock Música, el canal de música y las actualizaciones automáticas para OBS 31+. Va en la carpeta de OBS (Windows pide permiso). Cierra OBS antes."
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
  Delete "$INSTDIR\${APP_ID}.exe"
  Delete "$INSTDIR\icon.ico"
  Delete "$INSTDIR\Desinstalar.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${APP_ID}.lnk"
  Delete "$SMSTARTUP\${APP_ID}.lnk"
  Delete "$DESKTOP\${APP_ID}.lnk"
  ; El plugin solo se quita si lo instaló este instalador (la carpeta de OBS queda en el
  ; registro). Está en Archivos de programa: se borra con permiso de administrador.
  ReadRegStr $1 HKCU "Software\OBSNowPlaying" "PluginDir"
  ${If} $1 != ""
  ${AndIf} ${FileExists} "$1\obs-plugins\64bit\obs-now-playing.dll"
    ExecShellWait "runas" "cmd.exe" '/c del /f /q "$1\obs-plugins\64bit\obs-now-playing.dll" & rmdir /s /q "$1\data\obs-plugins\obs-now-playing"' SW_HIDE
  ${ElseIf} $1 != ""
  ${AndIf} ${FileExists} "$1\bin\64bit\obs-now-playing.dll"
    RMDir /r "$1" ; instalaciones de antes, en ProgramData
  ${EndIf}
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\OBSNowPlaying"
  ; La configuración y la sesión de Spotify quedan en %APPDATA%\OBS Now Playing por si reinstalas.
SectionEnd
