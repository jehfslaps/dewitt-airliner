#!/bin/bash
# Windows installer for Dewitt (NSIS).  make-installer.sh <folder with the plugin files> <output dir>
set -e
SRCDIR="$1"; OUT="$2"; HERE="$(cd "$(dirname "$0")" && pwd)"
PRETTY="Dewitt"; FILE="Dewitt"; KEY="Dewitt"; ART=clean
OLD='JAY'
cat > "$OUT/$FILE.nsi" <<NSI
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!define PRODUCT "$PRETTY"
!define VERSION "1.2"
Name "\${PRODUCT} \${VERSION}"
OutFile "$OUT/$FILE-1.2-Setup.exe"
InstallDir "\$PROGRAMFILES64\\slapworx\\\${PRODUCT}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "slapworx"
VIProductVersion "1.2.0.0"
VIAddVersionKey "ProductName" "\${PRODUCT}"
VIAddVersionKey "CompanyName" "slapworx"
VIAddVersionKey "FileVersion" "1.2.0"
VIAddVersionKey "FileDescription" "\${PRODUCT} installer"
VIAddVersionKey "LegalCopyright" "slapworx"

!define MUI_ICON "$HERE/$ART.ico"
!define MUI_UNICON "$HERE/$ART.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "$HERE/$ART-header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "$HERE/$ART-header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "$HERE/$ART-side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "$HERE/$ART-side.bmp"
!define MUI_WELCOMEPAGE_TITLE "Install \${PRODUCT} \${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Pad effects for live beat sets, by slapworx.\$\\r\$\\n\$\\r\$\\nThis puts \${PRODUCT} in the standard Windows plug-in folders, where your DAW (music software) looks for plug-ins. Close your DAW before you continue.\$\\r\$\\n\$\\r\$\\nAfterwards, open your DAW and let it find new plug-ins (most do this on start-up; some have a Rescan button in their plug-in settings)."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_TITLE "\${PRODUCT} is installed"
!define MUI_FINISHPAGE_TEXT "Open your DAW and let it find new plug-ins (most do this on start-up; some have a Rescan button). \${PRODUCT} is an audio effect: put it on a track or the master, and send your controller's MIDI to it."
!define MUI_FINISHPAGE_SHOWREADME "\$INSTDIR\\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the README"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  \${IfNot} \${RunningX64}
    MessageBox MB_ICONSTOP "\${PRODUCT} needs 64-bit Windows."
    Abort
  \${EndIf}
  SetRegView 64
FunctionEnd
Function un.onInit
  SetRegView 64
FunctionEnd

Section "CLAP plug-in" SecClap
  SetOutPath "\$COMMONFILES64\\CLAP"
  File "$SRCDIR/$FILE.clap"
SectionEnd

Section "VST3 plug-in" SecVst3
  SetOutPath "\$COMMONFILES64\\VST3"
  File /r "$SRCDIR/$FILE.vst3"
SectionEnd

Section "Remove old $OLD files" SecOld
NSI
cat >> "$OUT/$FILE.nsi" <<'NSI'
  Delete "$COMMONFILES64\CLAP\JAY.clap"
  RMDir /r "$COMMONFILES64\VST3\JAY.vst3"
NSI
cat >> "$OUT/$FILE.nsi" <<NSI
SectionEnd

Section "-files"
  SetOutPath "\$INSTDIR"
  File /oname=README.txt "$SRCDIR/README.md"
  WriteUninstaller "\$INSTDIR\\Uninstall \${PRODUCT}.exe"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "DisplayName" "\${PRODUCT} \${VERSION}"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "DisplayVersion" "\${VERSION}"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "Publisher" "slapworx"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "DisplayIcon" "\$INSTDIR\\Uninstall \${PRODUCT}.exe"
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "UninstallString" '"\$INSTDIR\\Uninstall \${PRODUCT}.exe"'
  WriteRegStr HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "InstallLocation" "\$INSTDIR"
  WriteRegDWORD HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "NoModify" 1
  WriteRegDWORD HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY" "NoRepair" 1
SectionEnd

LangString DESC_Clap \${LANG_ENGLISH} "$FILE.clap, for any DAW that supports CLAP."
LangString DESC_Vst3 \${LANG_ENGLISH} "$FILE.vst3, for any DAW that supports VST3 (most do)."
LangString DESC_Old \${LANG_ENGLISH} "Deletes the old $OLD plug-in files, so your DAW doesn't list the same plug-in twice. Your projects, settings and presets are kept."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT \${SecClap} \$(DESC_Clap)
  !insertmacro MUI_DESCRIPTION_TEXT \${SecVst3} \$(DESC_Vst3)
  !insertmacro MUI_DESCRIPTION_TEXT \${SecOld} \$(DESC_Old)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Delete "\$COMMONFILES64\\CLAP\\$FILE.clap"
  RMDir /r "\$COMMONFILES64\\VST3\\$FILE.vst3"
  Delete "\$INSTDIR\\README.txt"
  Delete "\$INSTDIR\\Uninstall \${PRODUCT}.exe"
  RMDir "\$INSTDIR"
  RMDir "\$PROGRAMFILES64\\slapworx"
  DeleteRegKey HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$KEY"
SectionEnd
NSI
makensis -V2 "$OUT/$FILE.nsi"
