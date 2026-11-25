#include "SettingsWindow.hpp"
#include "ActionRunner.hpp"
#include "ComHelpers.hpp"
#include "IconHelper.hpp"
#include "Theme.hpp"
#include "../resource.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <iterator>
#include <string>

std::atomic<bool> SettingsWindow::s_isOpen{ false };

namespace {

void EnsureCommonControls() noexcept {
    static bool s_initialized = false;
    if (!s_initialized) {
        INITCOMMONCONTROLSEX icex = {};
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);
        s_initialized = true;
    }
}

void CenterEditText(HWND hEdit) noexcept {
    RECT rc = {};
    GetClientRect(hEdit, &rc);
    const int clientH = rc.bottom - rc.top;

    HDC hdc = GetDC(hEdit);
    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hEdit, WM_GETFONT, 0, 0));
    HGDIOBJ hOld = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, hOld);
    ReleaseDC(hEdit, hdc);

    const int fontH = tm.tmHeight;
    const int padY = (clientH > fontH) ? (clientH - fontH) / 2 : 0;
    rc.top = padY;
    rc.bottom = padY + fontH;
    rc.left = 5;
    rc.right -= 5;
    SendMessageW(hEdit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rc));
}

} // namespace

SettingsWindow::~SettingsWindow() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    s_isOpen.store(false, std::memory_order_relaxed);
}

void SettingsWindow::Show() {
    EnsureCommonControls();
    Theme::EnsureResources();

    if (!m_hwnd || !IsWindow(m_hwnd)) {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        m_hwnd = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG), nullptr, DialogProc, reinterpret_cast<LPARAM>(this));
    }
    if (m_hwnd) {
        s_isOpen.store(true, std::memory_order_relaxed);
        ShowWindow(m_hwnd, SW_SHOW);
        if (IsIconic(m_hwnd)) {
            ShowWindow(m_hwnd, SW_RESTORE);
        }
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(m_hwnd);
        SetActiveWindow(m_hwnd);
    }
}

void SettingsWindow::Hide() {
    s_isOpen.store(false, std::memory_order_relaxed);
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    Theme::ReleaseResources();
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

bool SettingsWindow::IsVisible() const noexcept {
    return m_hwnd && IsWindow(m_hwnd) && IsWindowVisible(m_hwnd);
}

void SettingsWindow::OnCopilotKeyPressed() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        wchar_t buf[128];
        swprintf_s(buf, L"Copilot key pressed at %02u:%02u:%02u! Action executed.", st.wHour, st.wMinute, st.wSecond);
        SetDlgItemTextW(m_hwnd, IDC_STATUSBAR, buf);
    }
}

void SettingsWindow::Wakeup() {
    Show();
}

LRESULT CALLBACK SettingsWindow::GroupBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, GroupBoxSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_PAINT && Theme::IsSystemDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        DefSubclassProc(hwnd, uMsg, reinterpret_cast<WPARAM>(hdc), lParam);

        wchar_t text[128] = {};
        GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
        if (text[0] != L'\0') {
            HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ hOldFont = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));

            RECT rcText = {};
            DrawTextW(hdc, text, -1, &rcText, DT_CALCRECT | DT_SINGLELINE);
            const int textW = rcText.right - rcText.left;
            const int textH = rcText.bottom - rcText.top;

            RECT rcBg = { 8, 0, 8 + textW + 6, textH };
            FillRect(hdc, &rcBg, Theme::GetDarkBackgroundBrush());

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, Theme::GetDarkTextColor());
            RECT rcDraw = { 11, 0, 11 + textW, textH };
            DrawTextW(hdc, text, -1, &rcDraw, DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    switch (uMsg) {
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, EditSubclassProc, uIdSubclass);
        break;

    case WM_NCPAINT: {
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = GetWindowDC(hwnd);
            if (hdc) {
                RECT rc = {};
                GetWindowRect(hwnd, &rc);
                OffsetRect(&rc, -rc.left, -rc.top);

                HGDIOBJ oldPen = SelectObject(hdc, Theme::GetDarkEditBorderPen());
                HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPen);
                ReleaseDC(hwnd, hdc);
                return 0;
            }
        }
        break;
    }

    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (Theme::IsSystemDarkMode()) {
            RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        }
        return res;
    }

    case WM_SIZE:
        DefSubclassProc(hwnd, uMsg, wParam, lParam);
        CenterEditText(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            HWND hParent = GetParent(hwnd);
            if (hParent) {
                SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hParent, IDOK)));
            }
            return 0;
        }
        break;

    default:
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::ComboBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, ComboBoxSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_PAINT && Theme::IsSystemDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc = {};
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, Theme::GetDarkControlBrush());

        HGDIOBJ hOldPen = SelectObject(hdc, Theme::GetDarkComboBorderPen());
        HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);

        const int cur = static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
        if (cur >= 0) {
            wchar_t text[256] = {};
            SendMessageW(hwnd, CB_GETLBTEXT, cur, reinterpret_cast<LPARAM>(text));

            HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ hOldFont = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, Theme::GetDarkTextColor());

            RECT rcText = rc;
            rcText.left += 8;
            rcText.right -= 24;
            DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
        }

        const int arrowCenterX = rc.right - 14;
        const int arrowCenterY = (rc.top + rc.bottom) / 2;
        HGDIOBJ hOldArrowPen = SelectObject(hdc, Theme::GetDarkComboArrowPen());

        MoveToEx(hdc, arrowCenterX - 4, arrowCenterY - 2, nullptr);
        LineTo(hdc, arrowCenterX, arrowCenterY + 2);
        LineTo(hdc, arrowCenterX + 5, arrowCenterY - 3);

        SelectObject(hdc, hOldArrowPen);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::CheckboxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, CheckboxSubclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    if (uMsg == WM_PAINT && Theme::IsSystemDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc = {};
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, Theme::GetDarkBackgroundBrush());

        const bool isChecked = (SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);

        constexpr int boxSize = 18;
        constexpr int boxX = 0;
        int boxY = (rc.bottom - rc.top - boxSize) / 2;
        if (boxY < 0) boxY = 0;
        RECT rcBox = { boxX, boxY, boxX + boxSize, boxY + boxSize };

        if (isChecked) {
            HGDIOBJ oldPen = SelectObject(hdc, Theme::GetAccentPen());
            HGDIOBJ oldBr = SelectObject(hdc, Theme::GetAccentBrush());
            RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);

            SelectObject(hdc, Theme::GetWhiteCheckPen());
            MoveToEx(hdc, boxX + 4, boxY + 9, nullptr);
            LineTo(hdc, boxX + 7, boxY + 13);
            LineTo(hdc, boxX + 14, boxY + 5);

            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPen);
        } else {
            HGDIOBJ oldPen = SelectObject(hdc, Theme::GetDarkCheckboxBorderPen());
            HGDIOBJ oldBr = SelectObject(hdc, Theme::GetDarkControlBrush());
            RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);

            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPen);
        }

        wchar_t text[256] = {};
        GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
        if (text[0] != L'\0') {
            HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ hOldFont = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, Theme::GetDarkTextColor());

            RECT rcText = rc;
            rcText.left = boxX + boxSize + 8;
            DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK SettingsWindow::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* pThis = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_INITDIALOG: {
        pThis = reinterpret_cast<SettingsWindow*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        if (pThis) {
            pThis->m_hwnd = hwnd;
            pThis->OnInitDialog(hwnd);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        if (pThis) {
            pThis->OnCommand(hwnd, wParam, lParam);
        }
        return TRUE;
    }

    case WM_MEASUREITEM: {
        auto* pMIS = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (pMIS->CtlType == ODT_COMBOBOX) {
            pMIS->itemHeight = 22;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (pDIS->CtlType == ODT_COMBOBOX) {
            HDC hdc = pDIS->hDC;
            RECT rc = pDIS->rcItem;
            const bool dark = Theme::IsSystemDarkMode();
            const bool isSelected = (pDIS->itemState & ODS_SELECTED) != 0;

            if (dark) {
                FillRect(hdc, &rc, isSelected ? Theme::GetDarkComboSelectionBrush() : Theme::GetDarkControlBrush());
            } else {
                FillRect(hdc, &rc, GetSysColorBrush(isSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
            }

            const COLORREF fg = dark ? Theme::GetDarkTextColor()
                                     : (isSelected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT));

            if (pDIS->itemID != static_cast<UINT>(-1)) {
                wchar_t text[256] = {};
                SendMessageW(pDIS->hwndItem, CB_GETLBTEXT, pDIS->itemID, reinterpret_cast<LPARAM>(text));

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);

                RECT rcText = rc;
                rcText.left += 6;
                DrawTextW(hdc, text, -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORDLG:
        if (Theme::IsSystemDarkMode()) {
            return reinterpret_cast<INT_PTR>(Theme::GetDarkBackgroundBrush());
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hCtrl = reinterpret_cast<HWND>(lParam);
            SetTextColor(hdc, (GetDlgCtrlID(hCtrl) == IDC_STATUSBAR) ? Theme::GetDarkSubtextColor() : Theme::GetDarkTextColor());
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(Theme::GetDarkBackgroundBrush());
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, Theme::GetDarkTextColor());
            SetBkColor(hdc, Theme::GetDarkControlColor());
            return reinterpret_cast<INT_PTR>(Theme::GetDarkControlBrush());
        }
        break;

    case WM_CTLCOLORBTN:
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, Theme::GetDarkTextColor());
            SetBkColor(hdc, Theme::GetDarkBackgroundColor());
            return reinterpret_cast<INT_PTR>(Theme::GetDarkBackgroundBrush());
        }
        break;

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        Theme::HandleSettingChange(hwnd, lParam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_APP_COPILOT_PRESSED:
        if (pThis) {
            pThis->OnCopilotKeyPressed();
        }
        return TRUE;

    case WM_APP_WAKEUP:
        if (pThis) {
            pThis->Show();
        }
        return TRUE;

    case WM_CLOSE:
        if (pThis) {
            pThis->Hide();
        } else {
            DestroyWindow(hwnd);
        }
        return TRUE;

    case WM_NCDESTROY:
        s_isOpen.store(false, std::memory_order_relaxed);
        if (pThis) {
            pThis->m_hwnd = nullptr;
        }
        break;

    default:
        break;
    }
    return FALSE;
}

void SettingsWindow::OnInitDialog(HWND hwnd) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(IconHelper::GetAppIcon()));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(IconHelper::GetSmallAppIcon()));

    Theme::ApplyThemeToWindowAndChildren(hwnd);

    SetWindowSubclass(GetDlgItem(hwnd, IDC_GROUP_ACTION), GroupBoxSubclassProc, 1, 0);
    SetWindowSubclass(GetDlgItem(hwnd, IDC_GROUP_OPTIONS), GroupBoxSubclassProc, 2, 0);

    HWND hEditTarget = GetDlgItem(hwnd, IDC_EDIT_TARGET);
    HWND hEditArgs = GetDlgItem(hwnd, IDC_EDIT_ARGS);
    SetWindowSubclass(hEditTarget, EditSubclassProc, 3, 0);
    SetWindowSubclass(hEditArgs, EditSubclassProc, 4, 0);

    for (HWND hEdit : { hEditTarget, hEditArgs }) {
        const LONG_PTR exStyle = GetWindowLongPtrW(hEdit, GWL_EXSTYLE);
        if (exStyle & WS_EX_CLIENTEDGE) {
            SetWindowLongPtrW(hEdit, GWL_EXSTYLE, exStyle & ~WS_EX_CLIENTEDGE);
        }
        const LONG_PTR style = GetWindowLongPtrW(hEdit, GWL_STYLE);
        if (!(style & WS_BORDER)) {
            SetWindowLongPtrW(hEdit, GWL_STYLE, style | WS_BORDER);
        }
        SetWindowPos(hEdit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_ACTION);
    SetWindowSubclass(hCombo, ComboBoxSubclassProc, 5, 0);

    SetWindowSubclass(GetDlgItem(hwnd, IDC_CHECK_STARTUP), CheckboxSubclassProc, 6, 0);
    SetWindowSubclass(GetDlgItem(hwnd, IDC_CHECK_ADMIN), CheckboxSubclassProc, 7, 0);

    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Launch Application / File"));
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Open Website / URL"));
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Run PowerShell Command"));

    m_config = ConfigManager::GetCached();
    LoadConfigToUI();

    CenterEditText(hEditTarget);
    CenterEditText(hEditArgs);
}

void SettingsWindow::LoadConfigToUI() {
    HWND hCombo = GetDlgItem(m_hwnd, IDC_COMBO_ACTION);

    if (m_config.actionType == L"OpenUrl" || (!m_config.targetPath.empty() && m_config.targetPath.rfind(L"http", 0) == 0)) {
        SendMessageW(hCombo, CB_SETCURSEL, 1, 0);
    } else if (m_config.actionType == L"RunCommand") {
        SendMessageW(hCombo, CB_SETCURSEL, 2, 0);
    } else {
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
    }

    const wchar_t* target = m_config.targetPath.empty() ? L"wt.exe" : m_config.targetPath.c_str();
    SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, target);
    SetDlgItemTextW(m_hwnd, IDC_EDIT_ARGS, m_config.arguments.c_str());

    CheckDlgButton(m_hwnd, IDC_CHECK_STARTUP, m_config.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_CHECK_ADMIN, m_config.runAsAdmin ? BST_CHECKED : BST_UNCHECKED);

    UpdateTargetPrompt();
}

void SettingsWindow::UpdateTargetPrompt() {
    HWND hCombo = GetDlgItem(m_hwnd, IDC_COMBO_ACTION);
    const int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));

    RECT rcTarget = (sel == 1 || sel == 2) ? RECT{ 20, 59, 20 + 280, 59 + 12 } : RECT{ 20, 59, 20 + 224, 59 + 12 };
    MapDialogRect(m_hwnd, &rcTarget);

    HWND hTarget = GetDlgItem(m_hwnd, IDC_EDIT_TARGET);
    HWND hBrowse = GetDlgItem(m_hwnd, IDC_BTN_BROWSE);
    HWND hLblArgs = GetDlgItem(m_hwnd, IDC_LBL_ARGS);
    HWND hEditArgs = GetDlgItem(m_hwnd, IDC_EDIT_ARGS);
    HWND hCheckAdmin = GetDlgItem(m_hwnd, IDC_CHECK_ADMIN);

    wchar_t currentTarget[1024] = {};
    GetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, currentTarget, static_cast<int>(std::size(currentTarget)));
    const bool startsWithHttp = (wcsncmp(currentTarget, L"http", 4) == 0);

    bool showBrowse = false;
    bool showArgs = false;
    bool showAdmin = true;

    if (sel == 1) {
        SetDlgItemTextW(m_hwnd, IDC_LBL_TARGET, L"Website URL:");
        if (!startsWithHttp) {
            SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, L"https://claude.ai");
        }
        showAdmin = false;
    } else if (sel == 2) {
        SetDlgItemTextW(m_hwnd, IDC_LBL_TARGET, L"PowerShell command or script:");
        if (startsWithHttp) {
            SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, L"");
        }
    } else {
        SetDlgItemTextW(m_hwnd, IDC_LBL_TARGET, L"Application or file to open:");
        if (startsWithHttp) {
            SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, L"wt.exe");
        }
        showBrowse = true;
        showArgs = true;
    }

    HDWP hdwp = BeginDeferWindowPos(5);
    if (hdwp) {
        auto visFlag = [](bool visible) noexcept -> UINT {
            return SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
        };
        hdwp = DeferWindowPos(hdwp, hBrowse, nullptr, 0, 0, 0, 0, visFlag(showBrowse));
        if (hdwp) {
            hdwp = DeferWindowPos(hdwp, hTarget, nullptr, rcTarget.left, rcTarget.top,
                                  rcTarget.right - rcTarget.left, rcTarget.bottom - rcTarget.top,
                                  SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (hdwp) hdwp = DeferWindowPos(hdwp, hLblArgs, nullptr, 0, 0, 0, 0, visFlag(showArgs));
        if (hdwp) hdwp = DeferWindowPos(hdwp, hEditArgs, nullptr, 0, 0, 0, 0, visFlag(showArgs));
        if (hdwp) hdwp = DeferWindowPos(hdwp, hCheckAdmin, nullptr, 0, 0, 0, 0, visFlag(showAdmin));
        if (hdwp) EndDeferWindowPos(hdwp);
    }

    CenterEditText(hTarget);
}

void SettingsWindow::SaveUIToConfig() {
    HWND hCombo = GetDlgItem(m_hwnd, IDC_COMBO_ACTION);
    const int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
    m_config.actionType = (sel == 1) ? L"OpenUrl" : ((sel == 2) ? L"RunCommand" : L"LaunchApp");

    auto ReadDlgText = [this](int ctrlId, std::wstring& dest) {
        HWND hCtrl = GetDlgItem(m_hwnd, ctrlId);
        const int len = GetWindowTextLengthW(hCtrl);
        if (len <= 0) {
            dest.clear();
            return;
        }
        dest.resize(static_cast<size_t>(len) + 1);
        const int copied = GetWindowTextW(hCtrl, dest.data(), len + 1);
        dest.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    };

    ReadDlgText(IDC_EDIT_TARGET, m_config.targetPath);
    ReadDlgText(IDC_EDIT_ARGS, m_config.arguments);

    m_config.runAsAdmin = (IsDlgButtonChecked(m_hwnd, IDC_CHECK_ADMIN) == BST_CHECKED);
    m_config.startWithWindows = (IsDlgButtonChecked(m_hwnd, IDC_CHECK_STARTUP) == BST_CHECKED);
}

void SettingsWindow::OnBrowse(HWND hwnd) {
    CoInitGuard co;
    ComPtr<IFileOpenDialog> pDialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, pDialog.PutVoid()))) {
        return;
    }

    static constexpr COMDLG_FILTERSPEC filters[] = {
        { L"Applications & Shortcuts (*.exe;*.bat;*.cmd;*.lnk)", L"*.exe;*.bat;*.cmd;*.lnk" },
        { L"All Files (*.*)", L"*.*" }
    };
    pDialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    pDialog->SetTitle(L"Select Application or File");

    DWORD options = 0;
    if (SUCCEEDED(pDialog->GetOptions(&options))) {
        pDialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    }

    if (SUCCEEDED(pDialog->Show(hwnd))) {
        ComPtr<IShellItem> pItem;
        if (SUCCEEDED(pDialog->GetResult(pItem.GetAddressOf())) && pItem) {
            PWSTR pszFilePath = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)) && pszFilePath) {
                SetDlgItemTextW(hwnd, IDC_EDIT_TARGET, pszFilePath);
                CenterEditText(GetDlgItem(hwnd, IDC_EDIT_TARGET));
                CoTaskMemFree(pszFilePath);
            }
        }
    }
}

void SettingsWindow::OnCommand(HWND hwnd, WPARAM wParam, LPARAM /*lParam*/) {
    const WORD id = LOWORD(wParam);
    const WORD code = HIWORD(wParam);

    if (id == IDC_COMBO_ACTION && code == CBN_SELCHANGE) {
        UpdateTargetPrompt();
    } else if (id == IDC_BTN_BROWSE) {
        OnBrowse(hwnd);
    } else if (id == IDC_BTN_TEST) {
        SaveUIToConfig();
        ActionRunner::Execute(m_config);
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        wchar_t buf[128];
        swprintf_s(buf, L"Test action triggered at %02u:%02u:%02u! Action executed.", st.wHour, st.wMinute, st.wSecond);
        SetDlgItemTextW(hwnd, IDC_STATUSBAR, buf);
    } else if (id == IDOK) {
        const bool prevStartup = m_config.startWithWindows;
        SaveUIToConfig();
        ConfigManager::Save(m_config);
        if (m_config.startWithWindows || prevStartup) {
            ConfigManager::SetStartup(m_config.startWithWindows);
        }
        MessageBoxW(hwnd, L"Settings saved.", L"Copilot Remapper", MB_OK | MB_ICONINFORMATION);
    } else if (id == IDCANCEL) {
        Hide();
    }
}
