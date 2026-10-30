Unicode true
!define MUI_ICON "${__FILEDIR__}\RouteTec.ico"
!define MUI_UNICON "${__FILEDIR__}\RouteTec.ico"
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef STAGE
  !error "Pass /DSTAGE=absolute-path-to-staged-runtime"
!endif
!ifndef OUTPUT
  !error "Pass /DOUTPUT=absolute-path-to-installer"
!endif
!ifndef UNINSTALL_FILES
  !error "Pass /DUNINSTALL_FILES=absolute-path-to-generated-manifest"
!endif

Name "RouteTec"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\RouteTec"
InstallDirRegKey HKCU "Software\RouteTec" "InstallDir"
RequestExecutionLevel user
!ifdef QUICK_TEST
  SetCompressor zlib
!else
  SetCompressor /SOLID lzma
!endif
BrandingText "RouteTec 1.0.0"
VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=3082 "ProductName" "RouteTec"
VIAddVersionKey /LANG=3082 "FileDescription" "Instalador de RouteTec para Windows x64"
VIAddVersionKey /LANG=3082 "FileVersion" "1.0.0.0"
VIAddVersionKey /LANG=3082 "ProductVersion" "1.0.0"
VIAddVersionKey /LANG=3082 "LegalCopyright" "RouteTec"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Instalar RouteTec"
!define MUI_WELCOMEPAGE_TEXT "Este asistente instalará RouteTec 1.0.0 y las bibliotecas que necesita para funcionar.$\r$\n$\r$\nNo necesita instalar lenguajes de programación ni frameworks por separado.$\r$\n$\r$\nLa instalación es solo para su usuario y no modifica sus proyectos."
!define MUI_FINISHPAGE_TITLE "RouteTec está instalado"
!define MUI_FINISHPAGE_TEXT "Puede iniciar RouteTec desde el menú Inicio o el acceso directo del escritorio.$\r$\n$\r$\nEncontrará terrenos de práctica en la carpeta Ejemplos de la instalación."
!define MUI_FINISHPAGE_RUN "$INSTDIR\RouteTec.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir RouteTec"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Spanish"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "RouteTec necesita Windows de 64 bits."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "RouteTec necesita Windows 10 versión 1809 o posterior o Windows 11."
    Abort
  ${EndIf}
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentBuildNumber"
  IntCmp $0 17763 win_ok win_old win_ok
  win_old:
    MessageBox MB_ICONSTOP "Actualice Windows a la versión 1809 o posterior para instalar RouteTec."
    Abort
  win_ok:
  SetShellVarContext current
FunctionEnd

Section "RouteTec" SecMain
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\Desinstalar.exe"
  CreateDirectory "$SMPROGRAMS\RouteTec"
  CreateShortcut "$SMPROGRAMS\RouteTec\RouteTec.lnk" "$INSTDIR\RouteTec.exe"
  CreateShortcut "$SMPROGRAMS\RouteTec\Ejemplos.lnk" "$INSTDIR\Ejemplos"
  CreateShortcut "$SMPROGRAMS\RouteTec\Desinstalar.lnk" "$INSTDIR\Desinstalar.exe"
  CreateShortcut "$DESKTOP\RouteTec.lnk" "$INSTDIR\RouteTec.exe"
  WriteRegStr HKCU "Software\RouteTec" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "DisplayName" "RouteTec"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "DisplayVersion" "1.0.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "DisplayIcon" "$INSTDIR\RouteTec.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "QuietUninstallString" '"$INSTDIR\Desinstalar.exe" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\Desinstalar.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\RouteTec.lnk"
  Delete "$SMPROGRAMS\RouteTec\RouteTec.lnk"
  Delete "$SMPROGRAMS\RouteTec\Ejemplos.lnk"
  Delete "$SMPROGRAMS\RouteTec\Desinstalar.lnk"
  RMDir "$SMPROGRAMS\RouteTec"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RouteTec"
  DeleteRegKey HKCU "Software\RouteTec"
SectionEnd
