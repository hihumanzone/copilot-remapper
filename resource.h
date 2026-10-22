#pragma once

#define IDD_SETTINGS_DIALOG        101
#define IDR_TRAY_MENU              102
#define IDI_APP_ICON               103

// Dialog Controls
#define IDC_GROUP_ACTION           1001
#define IDC_LBL_ACTION             1002
#define IDC_COMBO_ACTION           1003
#define IDC_LBL_TARGET             1004
#define IDC_EDIT_TARGET            1005
#define IDC_BTN_BROWSE             1006
#define IDC_LBL_ARGS               1007
#define IDC_EDIT_ARGS              1008
#define IDC_GROUP_OPTIONS          1009
#define IDC_CHECK_STARTUP          1010
#define IDC_CHECK_ADMIN            1011
#define IDC_BTN_TEST               1012
#define IDC_STATUSBAR              1013

// Tray Menu Items
#define IDM_TRAY_SETTINGS          2001
#define IDM_TRAY_EXIT              2002

// Custom Window Messages
#define WM_APP_TRAY_CALLBACK       (WM_APP + 1)
#define WM_APP_COPILOT_PRESSED     (WM_APP + 2)
#define WM_APP_WAKEUP              (WM_APP + 3)
