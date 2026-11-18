; Airliner - Windows installer (NSIS 3, Modern UI 2). Built by make-installer-airliner.sh:
; makensis -DVERSION=1.0 -DNUMVER=1.0.0.0 -DSTAGE=<folder> -DOUTFILE=<exe> airliner.nsi
; STAGE holds the Airliner.vst3 bundle folder, Airliner.clap, README.txt and licenses/
Unicode true
Target amd64-unicode   ; a 64-bit installer (Airliner is 64-bit only)
ManifestDPIAware true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "1.0"
!endif
!ifndef NUMVER
  !define NUMVER "1.0.0.0"
!endif
!define PRODUCT "Airliner"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Airliner"

Name "${PRODUCT} ${VERSION}"
Caption "${PRODUCT} ${VERSION} setup"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\slapworx\${PRODUCT}"
RequestExecutionLevel admin
BrandingText "slapworx"
VIProductVersion "${NUMVER}"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "CompanyName" "slapworx"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT} installer"
VIAddVersionKey "LegalCopyright" "slapworx"

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define MUI_ICON "clean.ico"
!define MUI_UNICON "clean.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "clean-header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "clean-header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "clean-side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "clean-side.bmp"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC

!define MUI_WELCOMEPAGE_TITLE "Install ${PRODUCT} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "A DJ looper and a vinyl sim in one effect, by slapworx.$\r$\n$\r$\nThis puts ${PRODUCT} in the standard Windows plug-in folders, where your DAW (music software) looks for plug-ins.$\r$\n$\r$\nPlease close your DAW before you continue."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "${PRODUCT} is installed"
!define MUI_FINISHPAGE_TEXT "Open your DAW and let it scan for new plug-ins. Airliner is under slapworx.$\r$\n$\r$\nRight-click any control in Airliner to link your MIDI controller."
!define MUI_FINISHPAGE_SHOWREADME
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the README"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenReadme
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${PRODUCT} needs 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all   ; Start menu shortcuts for every user on this PC
FunctionEnd
Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd
Function OpenReadme
  ExecShell "open" "$INSTDIR\README.txt"
FunctionEnd

Section "VST3 plug-in" SecVst3
  ; a VST3 is a bundle folder: replace the whole thing so no old files linger
  RMDir /r "$COMMONFILES64\VST3\Airliner.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  File /r "${STAGE}/Airliner.vst3"
SectionEnd

Section "CLAP plug-in" SecClap
  SetOutPath "$COMMONFILES64\CLAP"
  ClearErrors
  File "${STAGE}/Airliner.clap"
  ${If} ${Errors}
    MessageBox MB_ICONEXCLAMATION "Airliner.clap couldn't be replaced. It's probably in use: close your DAW and run the installer again."
  ${EndIf}
SectionEnd

Section "-files"
  SetOutPath "$INSTDIR"
  File "${STAGE}/README.txt"
  SetOutPath "$INSTDIR\licenses"
  File "${STAGE}/licenses/*.txt"
  WriteUninstaller "$INSTDIR\Uninstall ${PRODUCT}.exe"
  CreateDirectory "$SMPROGRAMS\slapworx"
  CreateShortcut "$SMPROGRAMS\slapworx\Airliner README.lnk" "$INSTDIR\README.txt"
  CreateShortcut "$SMPROGRAMS\slapworx\Uninstall Airliner.lnk" "$INSTDIR\Uninstall ${PRODUCT}.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT} ${VERSION} (slapworx)"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "slapworx"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Uninstall ${PRODUCT}.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall ${PRODUCT}.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

LangString DESC_Vst3 ${LANG_ENGLISH} "Airliner.vst3, for any DAW that supports VST3 (most do)."
LangString DESC_Clap ${LANG_ENGLISH} "Airliner.clap, for any DAW that supports CLAP."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVst3} $(DESC_Vst3)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecClap} $(DESC_Clap)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  RMDir /r "$COMMONFILES64\VST3\Airliner.vst3"
  Delete "$COMMONFILES64\CLAP\Airliner.clap"
  Delete "$SMPROGRAMS\slapworx\Airliner README.lnk"
  Delete "$SMPROGRAMS\slapworx\Uninstall Airliner.lnk"
  RMDir "$SMPROGRAMS\slapworx"
  Delete "$INSTDIR\README.txt"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\Uninstall ${PRODUCT}.exe"
  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\slapworx"
  DeleteRegKey HKLM "${UNINST_KEY}"
  ; your settings and MIDI links (%APPDATA%) are left alone
SectionEnd
