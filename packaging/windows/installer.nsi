Unicode true
!include "MUI2.nsh"

!ifndef INPUT_DIR
  !error "INPUT_DIR is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif
!ifndef VERSION
  !define VERSION "0.0.0"
!endif

Name "nexPDF ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\nexPDF"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

Section "nexPDF" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${INPUT_DIR}\*"
  CreateDirectory "$SMPROGRAMS\nexPDF"
  CreateShortcut "$SMPROGRAMS\nexPDF\nexPDF.lnk" "$INSTDIR\bin\nexPDF.exe"
  CreateShortcut "$DESKTOP\nexPDF.lnk" "$INSTDIR\bin\nexPDF.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\nexPDF.lnk"
  RMDir /r "$SMPROGRAMS\nexPDF"
  RMDir /r "$INSTDIR"
SectionEnd
