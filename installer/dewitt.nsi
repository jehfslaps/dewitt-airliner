; Dewitt - Windows installer (NSIS 3, Modern UI 2). Built by make-installer.sh:
; makensis -DVERSION=1.0 -DNUMVER=1.0.0.0 -DSTAGE=<folder> -DOUTFILE=<exe> dewitt.nsi
; STAGE holds the Dewitt.vst3 bundle folder, Dewitt.clap, Dewitt_User_Manual.pdf, README.txt, licenses/ and "How to Set Up MIDI"/
Unicode true
Target amd64-unicode   ; a 64-bit installer (Dewitt is 64-bit only)
ManifestDPIAware true
SetCompressor /SOLID lzma

!ifndef VERSION
  !define VERSION "1.0"
!endif
!ifndef NUMVER
  !define NUMVER "1.0.0.0"
!endif
!define PRODUCT "Dewitt"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Dewitt"

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
!define MUI_WELCOMEPAGE_TEXT "40 pad effects for live beat sets, by slapworx.$\r$\n$\r$\nThis puts ${PRODUCT} in the standard Windows plug-in folders, where your DAW (music software) looks for plug-ins. It also installs the user manual and the How to Set Up MIDI pictures.$\r$\n$\r$\nPlease close your DAW before you continue."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "${PRODUCT} is installed"
!define MUI_FINISHPAGE_TEXT "Open your DAW and let it scan for new plug-ins. Dewitt is under slapworx.$\r$\n$\r$\nThe manual and MIDI pictures are in the Start menu under slapworx."
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open the user manual"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenManual
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_SHOWREADME
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Show the How to Set Up MIDI pictures"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION OpenMidi
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
Function OpenManual
  ExecShell "open" "$INSTDIR\Dewitt_User_Manual.pdf"
FunctionEnd
Function OpenMidi
  ExecShell "open" "$INSTDIR\How to Set Up MIDI"
FunctionEnd

Section "VST3 plug-in" SecVst3
  ; a VST3 is a bundle folder: replace the whole thing so no old files linger
  RMDir /r "$COMMONFILES64\VST3\Dewitt.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  File /r "${STAGE}/Dewitt.vst3"
SectionEnd

Section "CLAP plug-in" SecClap
  SetOutPath "$COMMONFILES64\CLAP"
  ClearErrors
  File "${STAGE}/Dewitt.clap"
  ${If} ${Errors}
    MessageBox MB_ICONEXCLAMATION "Dewitt.clap couldn't be replaced. It's probably in use: close your DAW and run the installer again."
  ${EndIf}
SectionEnd

Section "Remove the old JAY files" SecOld
  ; Dewitt's working name during testing. Projects, settings and presets are kept.
  Delete "$COMMONFILES64\CLAP\JAY.clap"
  RMDir /r "$COMMONFILES64\VST3\JAY.vst3"
SectionEnd

Section "-files"
  SetOutPath "$INSTDIR"
  File "${STAGE}/Dewitt_User_Manual.pdf"
  File "${STAGE}/README.txt"
  SetOutPath "$INSTDIR\licenses"
  File "${STAGE}/licenses/*.txt"
  SetOutPath "$INSTDIR\How to Set Up MIDI"
  File "${STAGE}/How to Set Up MIDI/*.png"
  WriteUninstaller "$INSTDIR\Uninstall ${PRODUCT}.exe"
  CreateDirectory "$SMPROGRAMS\slapworx"
  CreateShortcut "$SMPROGRAMS\slapworx\Dewitt user manual.lnk" "$INSTDIR\Dewitt_User_Manual.pdf"
  CreateShortcut "$SMPROGRAMS\slapworx\Dewitt - How to Set Up MIDI.lnk" "$INSTDIR\How to Set Up MIDI"
  CreateShortcut "$SMPROGRAMS\slapworx\Uninstall Dewitt.lnk" "$INSTDIR\Uninstall ${PRODUCT}.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT} ${VERSION} (slapworx)"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "slapworx"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\Uninstall ${PRODUCT}.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall ${PRODUCT}.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

LangString DESC_Vst3 ${LANG_ENGLISH} "Dewitt.vst3, for any DAW that supports VST3 (most do)."
LangString DESC_Clap ${LANG_ENGLISH} "Dewitt.clap, for any DAW that supports CLAP."
LangString DESC_Old ${LANG_ENGLISH} "Deletes the plug-in files of JAY (Dewitt's old name), so your DAW doesn't list it twice. Your projects, settings and presets are kept."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVst3} $(DESC_Vst3)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecClap} $(DESC_Clap)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecOld} $(DESC_Old)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  RMDir /r "$COMMONFILES64\VST3\Dewitt.vst3"
  Delete "$COMMONFILES64\CLAP\Dewitt.clap"
  Delete "$SMPROGRAMS\slapworx\Dewitt user manual.lnk"
  Delete "$SMPROGRAMS\slapworx\Dewitt - How to Set Up MIDI.lnk"
  Delete "$SMPROGRAMS\slapworx\Uninstall Dewitt.lnk"
  RMDir "$SMPROGRAMS\slapworx"
  Delete "$INSTDIR\Dewitt_User_Manual.pdf"
  Delete "$INSTDIR\README.txt"
  RMDir /r "$INSTDIR\licenses"
  Delete "$INSTDIR\How to Set Up MIDI\*.png"
  RMDir "$INSTDIR\How to Set Up MIDI"
  Delete "$INSTDIR\Uninstall ${PRODUCT}.exe"
  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\slapworx"
  DeleteRegKey HKLM "${UNINST_KEY}"
  ; your presets and settings (%APPDATA%) are left alone
SectionEnd
