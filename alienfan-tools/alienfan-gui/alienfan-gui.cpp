#include <windowsx.h>
#include <windows.h>
#include <powrprof.h>
#include "Resource.h"
#include "ConfigFan.h"
#include "MonHelper.h"
#include "common.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib,"PowrProf.lib")

using namespace std;

// Global Variables:
HINSTANCE hInst;                                // current instance
HWND sTip1 = 0, sTip2 = 0;

int idc_version = IDC_STATIC_VERSION, idc_homepage = IDC_SYSLINK_HOMEPAGE; // for About

ConfigFan* fan_conf = NULL;                     // Config...
MonHelper* mon = NULL;                          // Monitoring object

UINT newTaskBar = RegisterWindowMessage(TEXT("TaskbarCreated"));
HWND mDlg = NULL, fanWindow = NULL, tipWindow = NULL;

const char* pModes[] = { "�ر��Ƶ", "�����Ƶ", "����", "��Ч", "��Ч����", ""};

GUID* sch_guid, perfset;

NOTIFYICONDATA niDataFC;
extern NOTIFYICONDATA* niData;

bool isNewVersion = false;
bool needUpdateFeedback = false;
bool wasAWCC = false;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK    FanDialog(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    FanCurve(HWND, UINT, WPARAM, LPARAM);

extern void ReloadFanView(HWND list);
extern void ReloadPowerList(HWND list);
extern void ReloadTempView(HWND list);
extern void TempUIEvent(NMLVDISPINFO* lParam, HWND tempList);
extern void FanUIEvent(NMLISTVIEW* lParam, HWND fanList, HWND tempList);
extern string GetFanName(int ind, bool forTray = false);
extern void AlterGMode(HWND);
extern void DrawFan();

extern HANDLE ocStopEvent;
extern DWORD WINAPI CheckFanOverboost(LPVOID lpParam);

void SetHotkeys() {
    RegisterHotKey(mDlg, 6, 0, VK_F17);
    //power mode hotkeys
    for (int i = 0; i < mon->powerSize; i++)
        if (fan_conf->keyShortcuts)
            RegisterHotKey(mDlg, 20 + i, MOD_CONTROL | MOD_ALT, 0x30 + i);
        else
            UnregisterHotKey(mDlg, 20 + i);
}

void SetPowerState() {
    SYSTEM_POWER_STATUS state;
    GetSystemPowerStatus(&state);
    fan_conf->acPower = state.ACLineStatus;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    //UNREFERENCED_PARAMETER(lpCmdLine);
    //UNREFERENCED_PARAMETER(nCmdShow);

    ResetDPIScale(lpCmdLine);

    MSG msg{0};

    fan_conf = new ConfigFan();
    wasAWCC = DoStopAWCC(fan_conf->awcc_disable, true);
    SetPowerState();
    mon = new MonHelper();
    hInst = hInstance;

    niDataFC = { sizeof(NOTIFYICONDATA), 0, IDI_ALIENFANGUI, NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP, WM_APP + 1,
        (HICON)LoadImage(hInst,
            MAKEINTRESOURCE(IDI_ALIENFANGUI),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR) };

    niData = &niDataFC;

    if (mon->acpi->isSupported) {
        //if (fan_conf->needDPTF)
        //    CreateThread(NULL, 0, DPTFInit, fan_conf, 0, NULL);
        if (mDlg = CreateDialog(hInst, MAKEINTRESOURCE(IDD_MAIN_VIEW), NULL, (DLGPROC)FanDialog)) {

            SendMessage(mDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(hInst, MAKEINTRESOURCE(IDI_ALIENFANGUI)));
            SendMessage(mDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hInst, MAKEINTRESOURCE(IDI_ALIENFANGUI), IMAGE_ICON, 16, 16, 0));

            SetHotkeys();

            ShowWindow(mDlg, fan_conf->startMinimized ? SW_HIDE : SW_SHOW);
            ShowWindow(fanWindow, fan_conf->startMinimized ? SW_HIDE : SW_SHOWNA);

            // Main message loop:
            while ((GetMessage(&msg, 0, 0, 0)) != 0) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
    else {
        if (AddTrayIcon(niData, false)) {
            ShowNotification(niData, "Error", "Compatible hardware not found!");
            Sleep(5000);
        }
        WindowsStartSet(fan_conf->startWithWindows = false, "AlienFan-GUI");
    }
    delete mon;
    DoStopAWCC(wasAWCC, false);
    Shell_NotifyIcon(NIM_DELETE, niData);
    fan_conf->Save();
    delete fan_conf;
    return 0;
}

void RestoreApp() {
    SetTimer(mDlg, 0, fan_conf->pollingRate, NULL);
    ShowWindow(mDlg, SW_RESTORE);
    ShowWindow(fanWindow, SW_RESTORE);
    SendMessage(fanWindow, WM_PAINT, 0, 0);
    SetForegroundWindow(mDlg);
}

void ToggleValue(DWORD& value, int cID) {
    value = !value;
    CheckMenuItem(GetMenu(niData->hWnd), cID, value ? MF_CHECKED : MF_UNCHECKED);
}

void SetOCUI(HWND hDlg) {
    HWND tcc_slider = GetDlgItem(hDlg, IDC_SLIDER_TCC),
        xmp_slider = GetDlgItem(hDlg, IDC_SLIDER_XMP),
        tcc_edit = GetDlgItem(hDlg, IDC_EDIT_TCC);
    // OC block
    EnableWindow(tcc_slider, fan_conf->ocEnable && mon->acpi->isTcc);
    EnableWindow(tcc_edit, fan_conf->ocEnable && mon->acpi->isTcc);
    if (fan_conf->ocEnable && mon->acpi->isTcc) {
        SendMessage(tcc_slider, TBM_SETRANGE, true, MAKELPARAM(mon->acpi->maxTCC - mon->acpi->maxOffset, mon->acpi->maxTCC));
        sTip1 = CreateToolTip(tcc_slider, sTip1);
        SetSlider(sTip1, fan_conf->lastProf->currentTCC);
        SendMessage(tcc_slider, TBM_SETPOS, true, fan_conf->lastProf->currentTCC);

        // Set edit box value to match slider
        SetDlgItemInt(hDlg, IDC_EDIT_TCC, fan_conf->lastProf->currentTCC, FALSE);
    }
    EnableWindow(xmp_slider, fan_conf->ocEnable && mon->acpi->isXMP);
    if (fan_conf->ocEnable && mon->acpi->isXMP) {
        SendMessage(xmp_slider, TBM_SETRANGE, true, MAKELPARAM(0, 2));
        sTip2 = CreateToolTip(xmp_slider, sTip2);
        SetSlider(sTip2, fan_conf->lastProf->memoryXMP);
        SendMessage(xmp_slider, TBM_SETPOS, true, fan_conf->lastProf->memoryXMP);
    }
}

LRESULT CALLBACK FanDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND power_list = GetDlgItem(hDlg, IDC_COMBO_POWER),
        tcc_slider = GetDlgItem(hDlg, IDC_SLIDER_TCC),
        xmp_slider = GetDlgItem(hDlg, IDC_SLIDER_XMP),
        tempList = GetDlgItem(hDlg, IDC_TEMP_LIST),
        fanList = GetDlgItem(hDlg, IDC_FAN_LIST),
        tcc_edit = GetDlgItem(hDlg, IDC_EDIT_TCC);

    if (message == newTaskBar) {
        // Started/restarted explorer...
        AddTrayIcon(niData, fan_conf->updateCheck);
    }

    switch (message) {
    case WM_INITDIALOG:
    {
        niData->hWnd = hDlg;

        while (!AddTrayIcon(niData, fan_conf->updateCheck))
            Sleep(100);

        // set PerfBoost lists...
        IIDFromString(L"{be337238-0d82-4146-a960-4f3749d470c7}", &perfset);
        PowerGetActiveScheme(NULL, &sch_guid);
        DWORD acMode, dcMode;
        PowerReadACValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, &acMode);
        PowerReadDCValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, &dcMode);

        UpdateCombo(GetDlgItem(hDlg, IDC_AC_BOOST), pModes, acMode);
        UpdateCombo(GetDlgItem(hDlg, IDC_DC_BOOST), pModes, dcMode);;

        // So open fan control window...
        RECT cDlg;
        GetWindowRect(hDlg, &cDlg);
        int wh = cDlg.bottom - cDlg.top;
        tipWindow = fanWindow = CreateWindow("STATIC", "Fan curve", WS_CAPTION | WS_POPUP | WS_SIZEBOX,
                                 cDlg.right, cDlg.top, wh, wh,
                                 hDlg, NULL, hInst, 0);
        SetWindowLongPtr(fanWindow, GWLP_WNDPROC, (LONG_PTR) FanCurve);

        ReloadPowerList(power_list);
        ReloadFanView(fanList);

        SetTimer(hDlg, 0, fan_conf->pollingRate, NULL);

        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_STARTWITHWINDOWS, fan_conf->startWithWindows ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_STARTMINIMIZED, fan_conf->startMinimized ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_UPDATE, fan_conf->updateCheck ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_DISABLEAWCC, fan_conf->awcc_disable ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_KEYBOARDSHORTCUTS, fan_conf->keyShortcuts ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_RESTOREPOWERMODE, fan_conf->keepSystem ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_ENABLEOC, fan_conf->ocEnable ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_DISKSENSORS, fan_conf->diskSensors ? MF_CHECKED : MF_UNCHECKED);
        SetDlgItemInt(hDlg, IDC_EDIT_POLLING, fan_conf->pollingRate, false);

        // Set SystemID
        SetDlgItemText(hDlg, IDC_FC_ID, ("ID: " + to_string(mon->systemID)).c_str());

        // OC block
        SetOCUI(hDlg);

        return true;
    } break;
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId) {
        case IDC_AC_BOOST: case IDC_DC_BOOST: {
            switch (HIWORD(wParam)) {
            case CBN_SELCHANGE:
            {
                int cBst = ComboBox_GetCurSel(GetDlgItem(hDlg, wmId));
                if (wmId == IDC_AC_BOOST)
                    PowerWriteACValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, cBst);
                else
                    PowerWriteDCValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, cBst);
                PowerSetActiveScheme(NULL, sch_guid);
            } break;
            }
        } break;
        case IDC_EDIT_POLLING:
            if (HIWORD(wParam) == EN_KILLFOCUS) {
                mon->Stop();
                fan_conf->pollingRate = GetDlgItemInt(hDlg, IDC_EDIT_POLLING, NULL, false);
                mon->Start();
            }
            break;
        case IDOK: {
            RECT rect;
            ListView_GetSubItemRect(tempList, ListView_GetSelectionMark(tempList), 1, LVIR_LABEL, &rect);
            SetWindowPos(ListView_GetEditControl(tempList), HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0);
        } break;
        case IDC_COMBO_POWER:
        {
            switch (HIWORD(wParam)) {
            case CBN_SELCHANGE:
            {
                mon->SetPowerMode(ComboBox_GetCurSel(power_list));
            } break;
            case CBN_EDITCHANGE:
                if (mon->powerMode < mon->powerSize && mon->powerMode) {
                    char buffer[MAX_PATH];
                    GetWindowText(power_list, buffer, MAX_PATH);
                    fan_conf->powers[mon->acpi->powers[mon->powerMode]] = buffer;
                    ComboBox_DeleteString(power_list, mon->powerMode);
                    ComboBox_InsertString(power_list, mon->powerMode, buffer);
                }
                break;
            }
        } break;
        case IDC_BUT_MINIMIZE:
            SendMessage(hDlg, WM_SIZE, SIZE_MINIMIZED, 0);
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, About);
            break;
        case IDC_BUT_CLOSE: case IDM_EXIT:
            DestroyWindow(hDlg);
            break;
        case IDM_SAVE:
            fan_conf->Save();
            ShowNotification(niData, "���ñ������", "���ñ������");
            break;
        case IDM_SETTINGS_STARTWITHWINDOWS:
            ToggleValue(fan_conf->startWithWindows, wmId);
            WindowsStartSet(fan_conf->startWithWindows, "AlienFan-GUI");
            break;
        case IDM_SETTINGS_STARTMINIMIZED:
            ToggleValue(fan_conf->startMinimized, wmId);
            break;
        case IDM_SETTINGS_UPDATE:
            ToggleValue(fan_conf->updateCheck, wmId);
            if (fan_conf->updateCheck)
                CreateThread(NULL, 0, CUpdateCheck, niData, 0, NULL);
            break;
        case IDM_SETTINGS_KEYBOARDSHORTCUTS:
            ToggleValue(fan_conf->keyShortcuts, wmId);
            SetHotkeys();
            break;
        case IDM_SETTINGS_DISABLEAWCC:
            ToggleValue(fan_conf->awcc_disable, wmId);
            wasAWCC = DoStopAWCC((bool)fan_conf->awcc_disable != wasAWCC, wasAWCC);
            break;
        case IDM_SETTINGS_RESTOREPOWERMODE:
            ToggleValue(fan_conf->keepSystem, wmId);
            break;
        case IDM_SETTINGS_ENABLEOC:
            ToggleValue(fan_conf->ocEnable, wmId);
            mon->SetOC();
            SetOCUI(hDlg);
            break;
        case IDM_SETTINGS_DISKSENSORS:
            ToggleValue(fan_conf->diskSensors, wmId);
            KillTimer(hDlg, 0);
            delete mon;
            mon = new MonHelper();
            ReloadTempView(tempList);
            SetTimer(hDlg, 0, fan_conf->pollingRate, NULL);
            break;
        case IDC_FAN_RESET:
        {
            if (WarningBox(hDlg, "������з������ߣ�")) {
                fan_conf->lastProf->fanControls[fan_conf->lastSelectedFan].clear();
                ReloadTempView(tempList);
            }
        } break;
        case IDC_MAX_RESET:
            mon->maxTemps = mon->senValues;
            ReloadTempView(tempList);
            break;
        case IDC_BUT_OVER:
            if (mon->inControl) {
                CreateThread(NULL, 0, CheckFanOverboost, hDlg, 0, NULL);
            }
            else {
                SetEvent(ocStopEvent);
            }
            break;
        case IDC_BUT_RESETBOOST:
            if (mon->inControl)
                fan_conf->boosts[fan_conf->lastSelectedFan].maxBoost = 100;
            break;
        case IDC_EDIT_TCC:
            switch (HIWORD(wParam)) {
            case EN_CHANGE: {
                // Doesn't need to check success, 0 will be out of range and not changed
                int val = GetDlgItemInt(hDlg, IDC_EDIT_TCC, NULL, FALSE);
                // Did it in range?
                if (val && val == max(min(val, mon->acpi->maxTCC), mon->acpi->maxTCC - mon->acpi->maxOffset)) {
                    // Set slider and value
                    SendMessage(tcc_slider, TBM_SETPOS, TRUE, fan_conf->lastProf->currentTCC = val);
                    SetSlider(sTip1, val);
                    mon->SetOC();
                }
            } break;
            case EN_KILLFOCUS:
                // Just set resulted value
                SetDlgItemInt(hDlg, IDC_EDIT_TCC, fan_conf->lastProf->currentTCC, FALSE);
                break;
            } break;
        }
    }
    break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            // go to tray...
            KillTimer(hDlg, 0);
            ShowWindow(fanWindow, SW_HIDE);
            ShowWindow(hDlg, SW_HIDE);
        }
        break;
    case WM_MOVE:
    {
        // Reposition child...
        RECT cDlg;
        GetWindowRect(hDlg, &cDlg);
        SetWindowPos(fanWindow, hDlg, cDlg.right, cDlg.top, 0, 0, SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE);
    } break;
    case WM_APP + 2:
        EnableWindow(power_list, (bool)lParam);
        SetWindowText(GetDlgItem(hDlg, IDC_BUT_OVER), (bool)lParam ? "���\n ���ת��" : "ֹͣ");
        break;
    case WM_APP + 1:
    {
        switch (lParam) {
        case WM_RBUTTONUP: case WM_CONTEXTMENU:
        {
            POINT lpClickPoint;
            HMENU tMenu = LoadMenu(hInst, MAKEINTRESOURCEA(IDR_TRAYMENU));
            tMenu = GetSubMenu(tMenu, 0);
            MENUINFO mi{ sizeof(MENUINFO), MIM_STYLE, MNS_NOTIFYBYPOS /*| MNS_AUTODISMISS*/ };
            SetMenuInfo(tMenu, &mi);
            MENUITEMINFO mInfo{ sizeof(MENUITEMINFO), MIIM_STRING | MIIM_ID | MIIM_STATE };
            HMENU pMenu;
            pMenu = CreatePopupMenu();
            mInfo.wID = ID_TRAYMENU_POWER_SELECTED;
            for (int i = 0; i < mon->powerSize; i++) {
                mInfo.dwTypeData = (LPSTR)fan_conf->powers[mon->acpi->powers[i]].c_str();
                mInfo.fState = (i == fan_conf->lastProf->powerStage) ? MF_CHECKED : MF_UNCHECKED;
                InsertMenuItem(pMenu, i, false, &mInfo);
            }
            ModifyMenu(tMenu, ID_MENU_POWER, MF_BYCOMMAND | MF_STRING | MF_POPUP, (UINT_PTR)pMenu, ("����ģʽ -> " +
                fan_conf->powers[mon->acpi->powers[fan_conf->lastProf->powerStage]]).c_str());
            EnableMenuItem(tMenu, ID_MENU_GMODE, mon->acpi->isGmode ? MF_ENABLED : MF_DISABLED);
            CheckMenuItem(tMenu, ID_MENU_GMODE, fan_conf->lastProf->gmodeStage ? MF_CHECKED : MF_UNCHECKED);

            GetCursorPos(&lpClickPoint);
            SetForegroundWindow(hDlg);
            TrackPopupMenu(tMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
                lpClickPoint.x, lpClickPoint.y, 0, hDlg, NULL);
            PostMessage(hDlg, WM_NULL, 0, 0);
        } break;
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            RestoreApp();
            break;
        case NIN_BALLOONTIMEOUT:
            if (!isNewVersion) {
                Shell_NotifyIcon(NIM_DELETE, niData);
                Shell_NotifyIcon(NIM_ADD, niData);
            }
            else
                isNewVersion = false;
            break;
        case NIN_BALLOONUSERCLICK:
        {
            if (isNewVersion) {
                ShellExecute(NULL, "open", "https://github.com/T-Troll/alienfx-tools/releases", NULL, NULL, SW_SHOWNORMAL);
                isNewVersion = false;
            }
        } break;
        case WM_MOUSEMOVE: {
            string tooltip = string("Power: ") + (fan_conf->lastProf->gmodeStage ? "G-mode" : *fan_conf->GetPowerName(mon->acpi->powers[fan_conf->lastProf->powerStage]));
            for (int i = 0; i < mon->fansize; i++) {
                tooltip.append("\n" + GetFanName(i, true));
            }
            niData->szTip[127] = 0;
            strcpy_s(niData->szTip, min(127, tooltip.length() + 1), tooltip.c_str());
            Shell_NotifyIcon(NIM_MODIFY, niData);
        } break;
        }
        break;
    } break;
    case WM_HOTKEY:
        if (wParam == 6 || wParam > 19 && wParam - 20 < mon->powerSize) {
            if (wParam == 6) { // G-key for Dell G-series power switch
                AlterGMode(power_list);
                BlinkNumLock(mon->powerMode);
            }
            else { // Power mode shortcut
                mon->SetPowerMode((WORD)wParam - 20);
                BlinkNumLock(mon->powerMode);
            }
            ComboBox_SetCurSel(power_list, mon->powerMode);
            ShowNotification(niData, "Power mode switched!", "New power mode - " + (fan_conf->lastProf->gmodeStage ? "G-mode" : *fan_conf->GetPowerName(mon->acpi->powers[mon->powerMode])));
        }
        break;
    case WM_MENUCOMMAND: {
        int idx = LOWORD(wParam);
        switch (GetMenuItemID((HMENU)lParam, idx)) {
        case ID_MENU_EXIT:
            DestroyWindow(hDlg);
            break;
        case ID_MENU_RESTORE:
            RestoreApp();
            break;
        case ID_MENU_GMODE:
            AlterGMode(power_list);
            break;
        case ID_TRAYMENU_POWER_SELECTED:
            mon->SetPowerMode(idx);
            ComboBox_SetCurSel(power_list, mon->powerMode);
            break;
        }
    } break;
    case WM_NOTIFY:
        switch (((NMHDR*)lParam)->idFrom) {
        case IDC_FAN_LIST:
            FanUIEvent((NMLISTVIEW*)lParam, fanList, tempList);
            break;
        case IDC_TEMP_LIST:
            TempUIEvent((NMLVDISPINFO*)lParam, tempList);
            break;
        } break;
    case WM_HSCROLL:
        switch (LOWORD(wParam)) {
        case TB_THUMBTRACK: case TB_ENDTRACK: {
            if ((HWND)lParam == tcc_slider) {
                fan_conf->lastProf->currentTCC = (BYTE)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
                SetSlider(sTip1, fan_conf->lastProf->currentTCC);
                // Update edit box
                SetDlgItemInt(hDlg, IDC_EDIT_TCC, fan_conf->lastProf->currentTCC, FALSE);
                mon->SetOC();
            }
            if ((HWND)lParam == xmp_slider) {
                fan_conf->lastProf->memoryXMP = (BYTE)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
                SetSlider(sTip2, fan_conf->lastProf->memoryXMP);
                mon->SetOC();
            }
        } break;
        } break;
    case WM_CLOSE:
        SendMessage(hDlg, SW_SHOW, SIZE_MINIMIZED, 0);
        break;
    case WM_DESTROY:
        LocalFree(sch_guid);
        PostQuitMessage(0);
        break;
    case WM_ENDSESSION:
        // Shutdown/restart scheduled....
        fan_conf->Save();
        mon->Stop();
        LocalFree(sch_guid);
        exit(0);
    case WM_POWERBROADCAST:
        switch (wParam) {
        case PBT_APMRESUMEAUTOMATIC:
            SetPowerState();
            mon->Start();
            if (fan_conf->updateCheck) {
                //needUpdateFeedback = false;
                CreateThread(NULL, 0, CUpdateCheck, niData, 0, NULL);
            }
            break;
        case PBT_APMSUSPEND:
            // Sleep initiated.
            mon->Stop();
            fan_conf->Save();
            break;
        case PBT_APMPOWERSTATUSCHANGE:
            // ac/batt change
            SetPowerState();
            mon->SetCurrentMode();
            ComboBox_SetCurSel(power_list, mon->powerMode);
            break;
        }
        break;
    case WM_TIMER:
        //DebugPrint("Fans UI update...\n");
        if (mon->modified) {
            for (int i = 0; i < mon->sensorSize; i++) {
                string name = to_string(mon->senValues[mon->acpi->sensors[i].sid]) + " (" + to_string(mon->maxTemps[mon->acpi->sensors[i].sid]) + ")";
                ListView_SetItemText(tempList, i, 0, (LPSTR)name.c_str());
                name = fan_conf->GetSensorName(&mon->acpi->sensors[i]);
                ListView_SetItemText(tempList, i, 1, (LPSTR)name.c_str());
            }
            RECT cArea;
            GetClientRect(tempList, &cArea);
            ListView_SetColumnWidth(tempList, 0, LVSCW_AUTOSIZE);
            ListView_SetColumnWidth(tempList, 1, cArea.right - ListView_GetColumnWidth(tempList, 0));
        }
        for (int i = 0; i < mon->fansize; i++) {
            string name = GetFanName(i);
            ListView_SetItemText(fanList, i, 0, (LPSTR)name.c_str());
        }
        DrawFan();
        break;
    default: return false;
    }
    return true;
}
