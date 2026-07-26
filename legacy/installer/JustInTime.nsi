; ============================================================
; JustInTime.nsi
; Installer cho JustInTime (Agent Qt/C++ + Dashboard Python + Dashboard Go)
;
; Yêu cầu chuẩn bị trước khi build:
;   1. Build project bằng CMake (Release) như bình thường.
;      -> Nhờ CMakeLists.txt đã có bước POST_BUILD, thư mục output
;         (vd build/Release/) sẽ tự có sẵn:
;           JustInTime.exe
;           dashboard/            (Python dashboard)
;           dashboard-go/dashboard.exe
;   2. Chạy windeployqt để kéo DLL Qt cần thiết vào cùng thư mục:
;        windeployqt --release build\Release\JustInTime.exe
;   3. Tạo cấu trúc thư mục "dist" cạnh file .nsi này:
;
;        installer\
;          JustInTime.nsi        (file này)
;          LICENSE.txt           (tự viết, hoặc xoá dòng !insertmacro MUI_PAGE_LICENSE bên dưới)
;          dist\
;            core\                     <- JustInTime.exe + toàn bộ DLL Qt/plugin do windeployqt tạo
;            dashboard-python\         <- toàn bộ nội dung thư mục dashboard/ (Python)
;            dashboard-go\             <- dashboard.exe (Go)
;            vcredist\vc_redist.x64.exe   (tuỳ chọn, nếu muốn cài kèm VC++ Runtime)
;
;      Ví dụ lệnh copy (chạy từ thư mục build output, PowerShell):
;        robocopy . installer\dist\core /E /XD dashboard dashboard-go
;        robocopy dashboard installer\dist\dashboard-python /E
;        mkdir installer\dist\dashboard-go
;        copy dashboard-go\dashboard.exe installer\dist\dashboard-go\
;
;   4. Cài NSIS (https://nsis.sourceforge.io/), rồi build:
;        makensis installer\JustInTime.nsi
;      -> ra file JustInTime-Setup-<version>.exe
; ============================================================

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; ---------------- Thông tin chung ----------------

!define PRODUCT_NAME        "JustInTime"
!define PRODUCT_VERSION     "1.0.0"
!define PRODUCT_PUBLISHER   "LoPhong Corporation"
!define PRODUCT_WEB_SITE    "https://lonamphong.com"
!define PRODUCT_UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"
!define MAIN_EXE            "JustInTime.exe"

Name "${PRODUCT_NAME}"
OutFile "JustInTime-Setup-${PRODUCT_VERSION}.exe"
InstallDir "$PROGRAMFILES64\JustInTime"
InstallDirRegKey HKLM "Software\${PRODUCT_NAME}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUnInstDetails show

VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName"     "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName"     "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileVersion"     "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion"  "${PRODUCT_VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} Installer"

; Nếu có icon riêng, bỏ comment 2 dòng dưới và đặt file app.ico
; cạnh script này:
; !define MUI_ICON   "app.ico"
; !define MUI_UNICON "app.ico"

; ---------------- Giao diện (Modern UI) ----------------

!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\win.bmp"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY

; Start Menu folder chọn được (lưu lại lựa chọn trong registry)
var StartMenuFolder
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\${PRODUCT_NAME}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuFolder"
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${MAIN_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "$(LAUNCH_TEXT)"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Vietnamese"

LangString LAUNCH_TEXT ${LANG_ENGLISH} "Launch JustInTime now"
LangString LAUNCH_TEXT ${LANG_VIETNAMESE} "Khởi chạy JustInTime ngay"

LangString DESC_SecCore ${LANG_ENGLISH} "The JustInTime agent (required) - tracks app usage locally and syncs to Supabase if you log in."
LangString DESC_SecCore ${LANG_VIETNAMESE} "Ứng dụng lõi JustInTime (bắt buộc) - theo dõi hoạt động cục bộ và đồng bộ lên Supabase nếu đăng nhập."

LangString DESC_SecPyDash ${LANG_ENGLISH} "Python web dashboard (optional). Requires Python + pip install -r requirements.txt on this machine."
LangString DESC_SecPyDash ${LANG_VIETNAMESE} "Dashboard bằng Python (tuỳ chọn). Cần cài Python + pip install -r requirements.txt trên máy này."

LangString DESC_SecGoDash ${LANG_ENGLISH} "Go web dashboard (optional, recommended). Single .exe, works fully offline, no extra runtime needed."
LangString DESC_SecGoDash ${LANG_VIETNAMESE} "Dashboard bằng Go (tuỳ chọn, khuyến nghị). Chỉ 1 file .exe, chạy được cả khi offline, không cần cài thêm gì."

LangString DESC_SecDesktop ${LANG_ENGLISH} "Create a desktop shortcut."
LangString DESC_SecDesktop ${LANG_VIETNAMESE} "Tạo shortcut ngoài Desktop."

;LangString DESC_SecVCRedist ${LANG_ENGLISH} "Install the Visual C++ Runtime required by the Qt-based agent, if not already present."
;LangString DESC_SecVCRedist ${LANG_VIETNAMESE} "Cài Visual C++ Runtime mà ứng dụng Qt cần, nếu máy chưa có."

; ---------------- Kiểm tra bản cũ đang chạy ----------------

Function .onInit
    ; Không cho cài đè lên khi JustInTime.exe đang chạy - tránh
    ; lỗi "file in use" giữa chừng.
    System::Call 'kernel32::CreateMutex(i 0, i 0, t "JustInTimeInstallerMutex") i .r1 ?e'
    Pop $R0
    StrCmp $R0 0 +3
        MessageBox MB_OK|MB_ICONEXCLAMATION "$(^Name) installer is already running."
        Abort
FunctionEnd

; ---------------- Section: Core (bắt buộc) ----------------

Section "!JustInTime Agent" SecCore
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; Nếu app đang chạy, tắt đi trước khi ghi đè file (uninstall
    ; cũ / update tại chỗ).
    nsExec::Exec 'taskkill /IM "${MAIN_EXE}" /F'
    nsExec::Exec 'taskkill /IM "dashboard.exe" /F'

    File /r "dist\core\*.*"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "Software\${PRODUCT_NAME}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\${PRODUCT_NAME}" "Version" "${PRODUCT_VERSION}"

    ; Đăng ký vào Add/Remove Programs
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_WEB_SITE}"
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${MAIN_EXE}"
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr   ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "NoModify" 1
    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "NoRepair" 1

    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"

    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\JustInTime.lnk" "$INSTDIR\${MAIN_EXE}"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

; ---------------- Section: Python Dashboard (tuỳ chọn) ----------------

Section "Python Web Dashboard" SecPyDash
    SetOutPath "$INSTDIR\dashboard"
    File /r "dist\dashboard-python\*.*"

    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Web Dashboard (Python).lnk" \
            "$SYSDIR\cmd.exe" '/c start "" pythonw "$INSTDIR\dashboard\app.pyw"' \
            "$INSTDIR\${MAIN_EXE}"
    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

; ---------------- Section: Go Dashboard (tuỳ chọn) ----------------

Section "Local Dashboard (Go)" SecGoDash
    SetOutPath "$INSTDIR\dashboard-go"
    File /r "dist\dashboard-go\*.*"

    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Local Dashboard (Go).lnk" \
            "$INSTDIR\dashboard-go\dashboard.exe"
    !insertmacro MUI_STARTMENU_WRITE_END
SectionEnd

; ---------------- Section: Desktop shortcut (tuỳ chọn) ----------------

Section "Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\JustInTime.lnk" "$INSTDIR\${MAIN_EXE}"
SectionEnd

; ---------------- Section: VC++ Runtime (tuỳ chọn) ----------------

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore}     $(DESC_SecCore)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPyDash}   $(DESC_SecPyDash)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecGoDash}   $(DESC_SecGoDash)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop}  $(DESC_SecDesktop)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecVCRedist} $(DESC_SecVCRedist)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------- Uninstaller ----------------

Function un.onInit
    MessageBox MB_YESNO|MB_ICONQUESTION "Are you sure you want to remove ${PRODUCT_NAME}?" IDYES +2
    Abort
FunctionEnd

Section "Uninstall"
    nsExec::Exec 'taskkill /IM "${MAIN_EXE}" /F'
    nsExec::Exec 'taskkill /IM "dashboard.exe" /F'
    nsExec::Exec 'taskkill /IM "pythonw.exe" /F'

    ; Gỡ autostart mà 2 app này có thể đã tự đăng ký (JustInTime.exe
    ; qua Settings, dashboard.exe qua tray "Launch at login").
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "JustInTime"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "JustInTimeDashboard"

    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    RMDir /r "$SMPROGRAMS\$StartMenuFolder"
    Delete "$DESKTOP\JustInTime.lnk"

    RMDir /r "$INSTDIR"

    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
    DeleteRegKey HKLM "Software\${PRODUCT_NAME}"

    ; Dữ liệu người dùng (%APPDATA%\JustInTime: activity log, cài đặt,
    ; session đăng nhập) mặc định được GIỮ LẠI. Hỏi trước khi xoá.
    MessageBox MB_YESNO|MB_ICONQUESTION \
        "Also delete your JustInTime data (activity history, settings, login) in $APPDATA\JustInTime?" \
        IDNO keep_data
        RMDir /r "$APPDATA\JustInTime"
    keep_data:
SectionEnd