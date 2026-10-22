#include "SettingsWindow.hpp"
#include "ActionRunner.hpp"
#include "IconHelper.hpp"
#include "Theme.hpp"
#include "../resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vector>

SettingsWindow::SettingsWindow() {
}

SettingsWindow::~SettingsWindow() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void SettingsWindow::Show() {
    if (!m_hwnd || !IsWindow(m_hwnd)) {
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        m_hwnd = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS_DIALOG), nullptr, DialogProc, reinterpret_cast<LPARAM>(this));
    }
    ShowWindow(m_hwnd, SW_SHOW);
    ShowWindow(m_hwnd, SW_RESTORE);
    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
}

void SettingsWindow::Hide() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool SettingsWindow::IsVisible() const {
    return m_hwnd && IsWindow(m_hwnd) && IsWindowVisible(m_hwnd);
}

void SettingsWindow::OnCopilotKeyPressed() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_APP_COPILOT_PRESSED, 0, 0);
    }
}

void SettingsWindow::Wakeup() {
    Show();
}

static void CenterEditText(HWND hEdit) {
    RECT rc;
    GetClientRect(hEdit, &rc);
    int clientH = rc.bottom - rc.top;

    HDC hdc = GetDC(hEdit);
    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hEdit, WM_GETFONT, 0, 0));
    HGDIOBJ hOld = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, hOld);
    ReleaseDC(hEdit, hdc);

    int fontH = tm.tmHeight;
    int padY = (clientH > fontH) ? (clientH - fontH) / 2 : 0;
    rc.top = padY;
    rc.bottom = padY + fontH;
    rc.left = 5;
    rc.right -= 5;
    SendMessageW(hEdit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rc));
}

LRESULT CALLBACK SettingsWindow::GroupBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_PAINT && Theme::IsSystemDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        DefSubclassProc(hwnd, uMsg, reinterpret_cast<WPARAM>(hdc), lParam);

        wchar_t text[128] = { 0 };
        GetWindowTextW(hwnd, text, 128);
        if (text[0] != L'\0') {
            HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ hOldFont = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));

            RECT rcText = { 0 };
            DrawTextW(hdc, text, -1, &rcText, DT_CALCRECT | DT_SINGLELINE);
            int textW = rcText.right - rcText.left;
            int textH = rcText.bottom - rcText.top;

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

LRESULT CALLBACK SettingsWindow::EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_NCPAINT: {
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = GetWindowDC(hwnd);
            if (hdc) {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                OffsetRect(&rc, -rc.left, -rc.top);

                // Thin 1-pixel border matching button borders
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
                HGDIOBJ oldPen = SelectObject(hdc, hPen);
                HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPen);
                DeleteObject(hPen);
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

LRESULT CALLBACK SettingsWindow::ComboBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_PAINT && Theme::IsSystemDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH hBg = CreateSolidBrush(Theme::GetDarkControlColor());
        FillRect(hdc, &rc, hBg);
        DeleteObject(hBg);

        HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ hOldPen = SelectObject(hdc, hBorderPen);
        HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBorderPen);

        int cur = static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
        if (cur >= 0) {
            wchar_t text[256] = { 0 };
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

        int arrowCenterX = rc.right - 14;
        int arrowCenterY = (rc.top + rc.bottom) / 2;
        HPEN hArrowPen = CreatePen(PS_SOLID, 2, RGB(180, 180, 180));
        SelectObject(hdc, hArrowPen);

        MoveToEx(hdc, arrowCenterX - 4, arrowCenterY - 2, nullptr);
        LineTo(hdc, arrowCenterX, arrowCenterY + 2);
        LineTo(hdc, arrowCenterX + 5, arrowCenterY - 3);

        SelectObject(hdc, hOldPen);
        DeleteObject(hArrowPen);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::CheckboxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_PAINT && Theme::IsSystemDarkMode()) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, Theme::GetDarkBackgroundBrush());

        bool isChecked = (SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);

        // Checkbox box sized to 18x18 pixels, matching earlier design
        int boxSize = 18;
        int boxX = 0;
        int boxY = (rc.bottom - rc.top - boxSize) / 2;
        if (boxY < 0) boxY = 0;
        RECT rcBox = { boxX, boxY, boxX + boxSize, boxY + boxSize };

        if (isChecked) {
            HBRUSH hBlue = CreateSolidBrush(RGB(0, 103, 192));
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
            HGDIOBJ oldPen = SelectObject(hdc, hPen);
            HGDIOBJ oldBr = SelectObject(hdc, hBlue);
            RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);

            HPEN hCheckPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            SelectObject(hdc, hCheckPen);
            MoveToEx(hdc, boxX + 4, boxY + 9, nullptr);
            LineTo(hdc, boxX + 7, boxY + 13);
            LineTo(hdc, boxX + 14, boxY + 5);

            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPen);
            DeleteObject(hCheckPen);
            DeleteObject(hPen);
            DeleteObject(hBlue);
        } else {
            HBRUSH hDark = CreateSolidBrush(Theme::GetDarkControlColor());
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(130, 130, 130));
            HGDIOBJ oldPen = SelectObject(hdc, hPen);
            HGDIOBJ oldBr = SelectObject(hdc, hDark);
            RoundRect(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 4, 4);

            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPen);
            DeleteObject(hPen);
            DeleteObject(hDark);
        }

        wchar_t text[256] = { 0 };
        GetWindowTextW(hwnd, text, 256);
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
            bool dark = Theme::IsSystemDarkMode();
            bool isSelected = (pDIS->itemState & ODS_SELECTED);

            COLORREF bg = dark ? (isSelected ? RGB(60, 60, 60) : Theme::GetDarkControlColor())
                               : (isSelected ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW));
            COLORREF fg = dark ? Theme::GetDarkTextColor()
                               : (isSelected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT));

            HBRUSH hBr = CreateSolidBrush(bg);
            FillRect(hdc, &rc, hBr);
            DeleteObject(hBr);

            if (pDIS->itemID != static_cast<UINT>(-1)) {
                wchar_t text[256] = { 0 };
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

    case WM_CTLCOLORDLG: {
        if (Theme::IsSystemDarkMode()) {
            return reinterpret_cast<INT_PTR>(Theme::GetDarkBackgroundBrush());
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND hCtrl = reinterpret_cast<HWND>(lParam);
            if (GetDlgCtrlID(hCtrl) == IDC_STATUSBAR) {
                SetTextColor(hdc, Theme::GetDarkSubtextColor());
            } else {
                SetTextColor(hdc, Theme::GetDarkTextColor());
            }
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(Theme::GetDarkBackgroundBrush());
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, Theme::GetDarkTextColor());
            SetBkColor(hdc, Theme::GetDarkControlColor());
            return reinterpret_cast<INT_PTR>(Theme::GetDarkControlBrush());
        }
        break;
    }

    case WM_CTLCOLORBTN: {
        if (Theme::IsSystemDarkMode()) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, Theme::GetDarkTextColor());
            SetBkColor(hdc, Theme::GetDarkBackgroundColor());
            return reinterpret_cast<INT_PTR>(Theme::GetDarkBackgroundBrush());
        }
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED: {
        Theme::HandleSettingChange(hwnd, lParam);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_APP_COPILOT_PRESSED: {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[128];
        swprintf_s(buf, L"Copilot key pressed at %02d:%02d:%02d! Action executed.", st.wHour, st.wMinute, st.wSecond);
        SetDlgItemTextW(hwnd, IDC_STATUSBAR, buf);
        return TRUE;
    }

    case WM_APP_WAKEUP: {
        if (pThis) {
            pThis->Show();
        }
        return TRUE;
    }

    case WM_CLOSE: {
        ShowWindow(hwnd, SW_HIDE);
        return TRUE;
    }

    default:
        break;
    }
    return FALSE;
}

void SettingsWindow::OnInitDialog(HWND hwnd) {
    HICON hIcon = IconHelper::GetAppIcon();
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));

    Theme::ApplyThemeToWindowAndChildren(hwnd);

    // Subclass controls for dark mode styling
    SetWindowSubclass(GetDlgItem(hwnd, IDC_GROUP_ACTION), GroupBoxSubclassProc, 1, 0);
    SetWindowSubclass(GetDlgItem(hwnd, IDC_GROUP_OPTIONS), GroupBoxSubclassProc, 2, 0);

    HWND hEditTarget = GetDlgItem(hwnd, IDC_EDIT_TARGET);
    HWND hEditArgs = GetDlgItem(hwnd, IDC_EDIT_ARGS);
    SetWindowSubclass(hEditTarget, EditSubclassProc, 3, 0);
    SetWindowSubclass(hEditArgs, EditSubclassProc, 4, 0);

    for (HWND hEdit : { hEditTarget, hEditArgs }) {
        LONG_PTR exStyle = GetWindowLongPtrW(hEdit, GWL_EXSTYLE);
        if (exStyle & WS_EX_CLIENTEDGE) {
            SetWindowLongPtrW(hEdit, GWL_EXSTYLE, exStyle & ~WS_EX_CLIENTEDGE);
        }
        LONG_PTR style = GetWindowLongPtrW(hEdit, GWL_STYLE);
        if (!(style & WS_BORDER)) {
            SetWindowLongPtrW(hEdit, GWL_STYLE, style | WS_BORDER);
        }
        SetWindowTheme(hEdit, L"", L"");
        SetWindowPos(hEdit, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_ACTION);
    SetWindowSubclass(hCombo, ComboBoxSubclassProc, 5, 0);

    HWND hCheckStartup = GetDlgItem(hwnd, IDC_CHECK_STARTUP);
    HWND hCheckAdmin = GetDlgItem(hwnd, IDC_CHECK_ADMIN);
    SetWindowSubclass(hCheckStartup, CheckboxSubclassProc, 6, 0);
    SetWindowSubclass(hCheckAdmin, CheckboxSubclassProc, 7, 0);

    // Populate action ComboBox
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Launch Application / File"));
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Open Website / URL"));
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Run PowerShell Command"));

    m_config = ConfigManager::Load();
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

    std::wstring target = m_config.targetPath.empty() ? L"wt.exe" : m_config.targetPath;
    SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, target.c_str());
    SetDlgItemTextW(m_hwnd, IDC_EDIT_ARGS, m_config.arguments.c_str());

    CheckDlgButton(m_hwnd, IDC_CHECK_STARTUP, ConfigManager::IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_hwnd, IDC_CHECK_ADMIN, m_config.runAsAdmin ? BST_CHECKED : BST_UNCHECKED);

    UpdateTargetPrompt();
}

void SettingsWindow::UpdateTargetPrompt() {
    HWND hCombo = GetDlgItem(m_hwnd, IDC_COMBO_ACTION);
    int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));

    RECT rcFull = { 20, 59, 20 + 280, 59 + 12 };
    RECT rcNarrow = { 20, 59, 20 + 224, 59 + 12 };
    MapDialogRect(m_hwnd, &rcFull);
    MapDialogRect(m_hwnd, &rcNarrow);

    HWND hTarget = GetDlgItem(m_hwnd, IDC_EDIT_TARGET);
    HWND hBrowse = GetDlgItem(m_hwnd, IDC_BTN_BROWSE);

    wchar_t currentTarget[1024] = { 0 };
    GetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, currentTarget, 1024);
    std::wstring targetStr = currentTarget;

    if (sel == 1) { // URL
        SetDlgItemTextW(m_hwnd, IDC_LBL_TARGET, L"Website URL:");
        if (targetStr.rfind(L"http", 0) != 0) {
            SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, L"https://claude.ai");
        }
        ShowWindow(hBrowse, SW_HIDE);
        SetWindowPos(hTarget, nullptr, rcFull.left, rcFull.top, rcFull.right - rcFull.left, rcFull.bottom - rcFull.top, SWP_NOZORDER | SWP_FRAMECHANGED);
        CenterEditText(hTarget);
        ShowWindow(GetDlgItem(m_hwnd, IDC_LBL_ARGS), SW_HIDE);
        ShowWindow(GetDlgItem(m_hwnd, IDC_EDIT_ARGS), SW_HIDE);
        ShowWindow(GetDlgItem(m_hwnd, IDC_CHECK_ADMIN), SW_HIDE);
    } else if (sel == 2) { // PowerShell
        SetDlgItemTextW(m_hwnd, IDC_LBL_TARGET, L"PowerShell command or script:");
        if (targetStr.rfind(L"http", 0) == 0) {
            SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, L"");
        }
        ShowWindow(hBrowse, SW_HIDE);
        SetWindowPos(hTarget, nullptr, rcFull.left, rcFull.top, rcFull.right - rcFull.left, rcFull.bottom - rcFull.top, SWP_NOZORDER | SWP_FRAMECHANGED);
        CenterEditText(hTarget);
        ShowWindow(GetDlgItem(m_hwnd, IDC_LBL_ARGS), SW_HIDE);
        ShowWindow(GetDlgItem(m_hwnd, IDC_EDIT_ARGS), SW_HIDE);
        ShowWindow(GetDlgItem(m_hwnd, IDC_CHECK_ADMIN), SW_SHOW);
    } else { // Launch App
        SetDlgItemTextW(m_hwnd, IDC_LBL_TARGET, L"Application or file to open:");
        if (targetStr.rfind(L"http", 0) == 0) {
            SetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, L"wt.exe");
        }
        SetWindowPos(hTarget, nullptr, rcNarrow.left, rcNarrow.top, rcNarrow.right - rcNarrow.left, rcNarrow.bottom - rcNarrow.top, SWP_NOZORDER | SWP_FRAMECHANGED);
        CenterEditText(hTarget);
        ShowWindow(hBrowse, SW_SHOW);
        ShowWindow(GetDlgItem(m_hwnd, IDC_LBL_ARGS), SW_SHOW);
        ShowWindow(GetDlgItem(m_hwnd, IDC_EDIT_ARGS), SW_SHOW);
        ShowWindow(GetDlgItem(m_hwnd, IDC_CHECK_ADMIN), SW_SHOW);
    }
}

void SettingsWindow::SaveUIToConfig() {
    HWND hCombo = GetDlgItem(m_hwnd, IDC_COMBO_ACTION);
    int sel = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
    m_config.actionType = (sel == 1) ? L"OpenUrl" : ((sel == 2) ? L"RunCommand" : L"LaunchApp");

    wchar_t targetBuf[2048] = { 0 };
    GetDlgItemTextW(m_hwnd, IDC_EDIT_TARGET, targetBuf, 2048);
    m_config.targetPath = targetBuf;

    wchar_t argsBuf[2048] = { 0 };
    GetDlgItemTextW(m_hwnd, IDC_EDIT_ARGS, argsBuf, 2048);
    m_config.arguments = argsBuf;

    m_config.runAsAdmin = (IsDlgButtonChecked(m_hwnd, IDC_CHECK_ADMIN) == BST_CHECKED);
    m_config.startWithWindows = (IsDlgButtonChecked(m_hwnd, IDC_CHECK_STARTUP) == BST_CHECKED);
}

void SettingsWindow::OnBrowse(HWND hwnd) {
    wchar_t filename[MAX_PATH] = { 0 };

    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Applications & Shortcuts (*.exe;*.bat;*.cmd;*.lnk)\0*.exe;*.bat;*.cmd;*.lnk\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select Application or File";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(hwnd, IDC_EDIT_TARGET, filename);
        CenterEditText(GetDlgItem(hwnd, IDC_EDIT_TARGET));
    }
}

void SettingsWindow::OnCommand(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    WORD id = LOWORD(wParam);
    WORD code = HIWORD(wParam);

    if (id == IDC_COMBO_ACTION && code == CBN_SELCHANGE) {
        UpdateTargetPrompt();
    } else if (id == IDC_BTN_BROWSE) {
        OnBrowse(hwnd);
    } else if (id == IDC_BTN_TEST) {
        SaveUIToConfig();
        ActionRunner::Execute(m_config);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[128];
        swprintf_s(buf, L"Test action triggered at %02d:%02d:%02d! Action executed.", st.wHour, st.wMinute, st.wSecond);
        SetDlgItemTextW(hwnd, IDC_STATUSBAR, buf);
    } else if (id == IDOK) { // Save
        SaveUIToConfig();
        ConfigManager::Save(m_config);
        ConfigManager::SetStartup(m_config.startWithWindows);
        MessageBoxW(hwnd, L"Settings saved.", L"Copilot Remapper", MB_OK | MB_ICONINFORMATION);
    } else if (id == IDCANCEL) { // Close
        Hide();
    }
}
