; SafeZone Overlay OBS Plugin - NSIS Installer Script
; Reads product name and version from environment variables set by the workflow

!define PRODUCT_NAME "$%PLUGIN_NAME%"
!define PRODUCT_VERSION "$%PLUGIN_VERSION%"
!define PLUGIN_DLL "obs-safezone-overlay.dll"
!define OBS_PLUGIN_DIR64 "$PROGRAMFILES64\obs-studio\obs-plugins\64bit"
!define OBS_DATA_DIR     "$PROGRAMFILES64\obs-studio\data\obs-plugins\obs-safezone-overlay"

; -----------------------------------------------------------------------
; General
; -----------------------------------------------------------------------
Name          "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile       "$%OUTPUT_PATH%\${PRODUCT_NAME}-${PRODUCT_VERSION}-windows-x64-installer.exe"
InstallDir    "$PROGRAMFILES64\obs-studio"
InstallDirRegKey HKLM "Software\OBS Studio" ""
RequestExecutionLevel admin

; -----------------------------------------------------------------------
; Interface settings
; -----------------------------------------------------------------------
!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "$%PROJECT_ROOT%\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; -----------------------------------------------------------------------
; Installer section
; -----------------------------------------------------------------------
Section "SafeZone Overlay Plugin" SecMain
    SectionIn RO

    ; Plugin DLL -> obs-plugins\64bit
    SetOutPath "${OBS_PLUGIN_DIR64}"
    File "$%RELEASE_DIR%\${PLUGIN_DLL}"

    ; Plugin data -> data\obs-plugins\obs-safezone-overlay
    SetOutPath "${OBS_DATA_DIR}"
    File /r /nonfatal "$%RELEASE_DIR%\..\data\*.*"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\uninstall-safezone-overlay.exe"

    ; Registry entry for Add/Remove Programs
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\SafeZoneOverlay" \
        "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\SafeZoneOverlay" \
        "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\SafeZoneOverlay" \
        "Publisher" "Dcoderz Philippines"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\SafeZoneOverlay" \
        "UninstallString" "$INSTDIR\uninstall-safezone-overlay.exe"
SectionEnd

; -----------------------------------------------------------------------
; Uninstaller section
; -----------------------------------------------------------------------
Section "Uninstall"
    Delete "${OBS_PLUGIN_DIR64}\${PLUGIN_DLL}"
    RMDir /r "${OBS_DATA_DIR}"
    Delete "$INSTDIR\uninstall-safezone-overlay.exe"
    DeleteRegKey HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\SafeZoneOverlay"
SectionEnd
