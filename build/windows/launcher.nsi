; Lanzador "OBS Now Playing.exe": arranca node.exe + server.js sin ventana de consola.
; Doble clic: abre el panel en el navegador. Con --background (inicio de Windows): no lo abre.
Unicode true
!include "FileFunc.nsh"

Name "OBS Now Playing"
OutFile "${STAGE}\OBS Now Playing.exe"
Icon "${STAGE}\icon.ico"
RequestExecutionLevel user
SilentInstall silent

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "OBS Now Playing"
VIAddVersionKey "FileDescription" "OBS Now Playing"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT"

Section
  ${GetParameters} $0
  ${GetOptions} $0 "--background" $1
  IfErrors 0 background
    ExecShell "open" "$EXEDIR\node.exe" '"$EXEDIR\app\server.js" --open --log' SW_HIDE
    Goto done
  background:
    ExecShell "open" "$EXEDIR\node.exe" '"$EXEDIR\app\server.js" --log' SW_HIDE
  done:
SectionEnd
