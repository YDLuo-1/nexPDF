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
!ifndef ICON_FILE
  !error "ICON_FILE is required"
!endif
!ifndef LICENSE_FILE
  !error "LICENSE_FILE is required"
!endif

Name "nexPDF ${VERSION}"
OutFile "${OUTPUT_FILE}"
Icon "${ICON_FILE}"
InstallDir "$LOCALAPPDATA\nexPDF"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
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

  ; Polite per-user file association: register a PROGID and an "Open with"
  ; candidate under HKCU only, and expose nexPDF in Settings > Default apps so
  ; the user decides. Never write the .pdf default value or HKCR directly.
  WriteRegStr HKCU "Software\Classes\nexPDF.pdf" "" "nexPDF Document"
  WriteRegStr HKCU "Software\Classes\nexPDF.pdf\DefaultIcon" "" "$INSTDIR\bin\nexPDF.exe"
  WriteRegStr HKCU "Software\Classes\nexPDF.pdf\shell\open\command" "" '"$INSTDIR\bin\nexPDF.exe" "%1"'
  WriteRegStr HKCU "Software\Classes\.pdf\OpenWithProgids" "nexPDF.pdf" ""
  WriteRegStr HKCU "Software\nexPDF\Capabilities" "ApplicationName" "nexPDF"
  WriteRegStr HKCU "Software\nexPDF\Capabilities" "ApplicationDescription" "Local PDF viewer and practical editor"
  WriteRegStr HKCU "Software\nexPDF\Capabilities\FileAssociations" ".pdf" "nexPDF.pdf"
  WriteRegStr HKCU "Software\RegisteredApplications" "nexPDF" "Software\nexPDF\Capabilities"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\nexPDF.lnk"
  RMDir /r "$SMPROGRAMS\nexPDF"
  DeleteRegKey HKCU "Software\Classes\nexPDF.pdf"
  DeleteRegValue HKCU "Software\Classes\.pdf\OpenWithProgids" "nexPDF.pdf"
  DeleteRegKey /ifempty HKCU "Software\Classes\.pdf\OpenWithProgids"
  DeleteRegKey HKCU "Software\nexPDF\Capabilities\FileAssociations"
  DeleteRegKey /ifempty HKCU "Software\nexPDF\Capabilities"
  DeleteRegKey /ifempty HKCU "Software\nexPDF"
  DeleteRegValue HKCU "Software\RegisteredApplications" "nexPDF"
  RMDir /r "$INSTDIR"
SectionEnd
