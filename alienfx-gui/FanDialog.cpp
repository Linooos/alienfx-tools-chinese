#include "alienfx-gui.h"
#include "EventHandler.h"
#include <powrprof.h>

#pragma comment(lib, "PowrProf.lib")

extern void SwitchTab(int);
extern HWND CreateToolTip(HWND hwndParent, HWND oldTip);

extern EventHandler* eve;
extern AlienFan_SDK::Control* acpi;
ConfigFan* fan_conf = NULL;
MonHelper* mon = NULL;
HWND fanWindow = NULL, tipWindow = NULL;

int pLid = -1;

GUID* sch_guid, perfset;

extern INT_PTR CALLBACK FanCurve(HWND, UINT, WPARAM, LPARAM);
void UpdateFanUI(LPVOID);

ThreadHelper* fanUIUpdate;

void ReloadFanView(HWND hDlg, int cID) {
    temp_block* sen = conf->fan_conf->FindSensor(conf->fan_conf->lastSelectedSensor);
    HWND list = GetDlgItem(hDlg, IDC_FAN_LIST);
    ListView_DeleteAllItems(list);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    if (!ListView_GetColumnWidth(list, 0)) {
        LVCOLUMNA lCol{ LVCF_FMT, LVCFMT_LEFT };
        ListView_InsertColumn(list, 0, &lCol);
    }
    for (int i = 0; i < acpi->HowManyFans(); i++) {
        LVITEMA lItem{ LVIF_TEXT | LVIF_PARAM, i};
        string name = "Fan " + to_string(i + 1) + " (" + to_string(acpi->GetFanRPM(i)) + ")";
        lItem.lParam = i;
        lItem.pszText = (LPSTR) name.c_str();
        if (i == cID) {
            lItem.mask |= LVIF_STATE;
            lItem.state = LVIS_SELECTED;
            SendMessage(fanWindow, WM_PAINT, 0, 0);
        }
        ListView_InsertItem(list, &lItem);
        if (sen && conf->fan_conf->FindFanBlock(sen, i)) {
            //conf->fan_conf->lastSelectedSensor = -1;
            ListView_SetCheckState(list, i, true);
            //conf->fan_conf->lastSelectedSensor = sen->sensorIndex;
        }
    }

    ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE_USEHEADER);
}

void ReloadPowerList(HWND hDlg, int id) {
    HWND list = GetDlgItem(hDlg, IDC_COMBO_POWER);
    ComboBox_ResetContent(list);
    for (int i = 0; i < acpi->HowManyPower(); i++) {
        string name;
        if (i) {
            auto pwr = conf->fan_conf->powers.find(acpi->powers[i]);
            name = pwr != conf->fan_conf->powers.end() ? pwr->second : "Level " + to_string(i);
        }
        else
            name = "Manual";
        int pos = ComboBox_AddString(list, (LPARAM)(name.c_str()));
        ComboBox_SetItemData(list, pos, i);
        if (i == id) {
            ComboBox_SetCurSel(list, pos);
            pLid = pos;
        }
    }
}

void ReloadTempView(HWND hDlg, int cID) {
    int rpos = 0;
    HWND list = GetDlgItem(hDlg, IDC_TEMP_LIST);
    ListView_DeleteAllItems(list);
    ListView_SetExtendedListViewStyle(list, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT);
    if (!ListView_GetColumnWidth(list, 1)) {
        LVCOLUMNA lCol{ LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM ,LVCFMT_LEFT,100,(LPSTR)"Temp",4};
        ListView_InsertColumn(list, 0, &lCol);
        lCol.pszText = (LPSTR)"Name";
        lCol.iSubItem = 1;
        ListView_InsertColumn(list, 1, &lCol);
    }
    for (int i = 0; i < acpi->HowManySensors(); i++) {
        LVITEMA lItem{ LVIF_TEXT | LVIF_PARAM, i };
        string name = to_string(acpi->GetTempValue(i)) + " (" + to_string(eve->mon->maxTemps[i]) + ")";
        lItem.lParam = i;
        lItem.pszText = (LPSTR) name.c_str();
        if (i == cID) {
            lItem.mask |= LVIF_STATE;
            lItem.state = LVIS_SELECTED;
            rpos = i;
        }
        ListView_InsertItem(list, &lItem);
        ListView_SetItemText(list, i, 1, (LPSTR) acpi->sensors[i].name.c_str());
    }
    ListView_SetColumnWidth(list, 0, LVSCW_AUTOSIZE);
    ListView_SetColumnWidth(list, 1, LVSCW_AUTOSIZE_USEHEADER);
    ListView_EnsureVisible(list, rpos, false);
}

BOOL CALLBACK TabFanDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND power_list = GetDlgItem(hDlg, IDC_COMBO_POWER),
        power_gpu = GetDlgItem(hDlg, IDC_SLIDER_GPU);

    switch (message) {
    case WM_INITDIALOG:
    {
        if (acpi && acpi->IsActivated()) {

            // set PerfBoost lists...
            HWND boost_ac = GetDlgItem(hDlg, IDC_AC_BOOST),
                boost_dc = GetDlgItem(hDlg, IDC_DC_BOOST);
            IIDFromString(L"{be337238-0d82-4146-a960-4f3749d470c7}", &perfset);
            PowerGetActiveScheme(NULL, &sch_guid);
            DWORD acMode, dcMode;
            PowerReadACValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, &acMode);
            PowerReadDCValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, &dcMode);

            char buffer[64];
            LoadString(hInst, IDS_BOOST_OFF, buffer, 64);
            ComboBox_AddString(boost_ac, buffer);
            ComboBox_AddString(boost_dc, buffer);
            LoadString(hInst, IDS_BOOST_ON, buffer, 64);
            ComboBox_AddString(boost_ac, buffer);
            ComboBox_AddString(boost_dc, buffer);
            LoadString(hInst, IDS_BOOST_AGGRESSIVE, buffer, 64);
            ComboBox_AddString(boost_ac, buffer);
            ComboBox_AddString(boost_dc, buffer);
            LoadString(hInst, IDS_BOOST_EFFICIENT, buffer, 64);
            ComboBox_AddString(boost_ac, buffer);
            ComboBox_AddString(boost_dc, buffer);
            LoadString(hInst, IDS_BOOST_EFFAGGR, buffer, 64);
            ComboBox_AddString(boost_ac, buffer);
            ComboBox_AddString(boost_dc, buffer);

            ComboBox_SetCurSel(boost_ac, acMode);
            ComboBox_SetCurSel(boost_dc, dcMode);

            ReloadPowerList(hDlg, conf->fan_conf->lastProf->powerStage);
            ReloadTempView(hDlg, conf->fan_conf->lastSelectedSensor);
            ReloadFanView(hDlg, conf->fan_conf->lastSelectedFan);

            // So open fan control window...
            fan_conf = conf->fan_conf;
            mon = eve->mon;
            fanWindow = GetDlgItem(hDlg, IDC_FAN_CURVE);
            SetWindowLongPtr(fanWindow, GWLP_WNDPROC, (LONG_PTR) FanCurve);
            sTip1 = CreateToolTip(fanWindow, NULL);
            tipWindow = GetDlgItem(hDlg, IDC_FC_LABEL);

            // Start UI update thread...
            fanUIUpdate = new ThreadHelper(UpdateFanUI, hDlg);
            //uiFanHandle = CreateThread(NULL, 0, UpdateFanUI, hDlg, 0, NULL);

            SendMessage(power_gpu, TBM_SETRANGE, true, MAKELPARAM(0, 4));
            SendMessage(power_gpu, TBM_SETTICFREQ, 1, 0);
            SendMessage(power_gpu, TBM_SETPOS, true, conf->fan_conf->lastProf->GPUPower);

        } else {
            SwitchTab(TAB_SETTINGS);
            return false;
        }
        return true;
    } break;
    case WM_COMMAND:
    {
        // Parse the menu selections:
        switch (LOWORD(wParam)) {
        case IDC_AC_BOOST: case IDC_DC_BOOST: {
            switch (HIWORD(wParam)) {
            case CBN_SELCHANGE:
            {
                int cBst = ComboBox_GetCurSel(GetDlgItem(hDlg, LOWORD(wParam)));
                if (LOWORD(wParam) == IDC_AC_BOOST)
                    PowerWriteACValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, cBst);
                else
                    PowerWriteDCValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, cBst);
                PowerSetActiveScheme(NULL, sch_guid);
            } break;
            }
        } break;
        case IDC_COMBO_POWER:
        {
            switch (HIWORD(wParam)) {
            case CBN_SELCHANGE:
            {
                pLid = ComboBox_GetCurSel(power_list);
                int pid = (int)ComboBox_GetItemData(power_list, pLid);
                conf->fan_conf->lastProf->powerStage = pid;
                acpi->SetPower(pid);
                conf->fan_conf->Save();
            } break;
            case CBN_EDITCHANGE:
            {
                char buffer[MAX_PATH];
                GetWindowTextA(power_list, buffer, MAX_PATH);
                if (pLid > 0) {
                    auto ret = conf->fan_conf->powers.emplace(acpi->powers[conf->fan_conf->lastProf->powerStage], buffer);
                    if (!ret.second)
                        // just update...
                        ret.first->second = buffer;
                    conf->fan_conf->Save();
                    ComboBox_DeleteString(power_list, pLid);
                    ComboBox_InsertString(power_list, pLid, buffer);
                    ComboBox_SetItemData(power_list, pLid, conf->fan_conf->lastProf->powerStage);
                }
                break;
            }
            }
        } break;
        case IDC_BUT_RESET:
        {
            temp_block* cur = conf->fan_conf->FindSensor(conf->fan_conf->lastSelectedSensor);
            if (cur) {
                fan_block* fan = conf->fan_conf->FindFanBlock(cur, conf->fan_conf->lastSelectedFan);
                if (fan) {
                    fan->points.clear();
                    fan->points.push_back({0,0});
                    fan->points.push_back({100,100});
                    SendMessage(fanWindow, WM_PAINT, 0, 0);
                }
            }
        } break;
        case IDC_MAX_RESET:
        {
            for (int i = 0; i < acpi->HowManySensors(); i++)
                eve->mon->maxTemps[i] = acpi->GetTempValue(i);
            ReloadTempView(hDlg, conf->fan_conf->lastSelectedSensor);
        } break;
        }
    } break;
    case WM_NOTIFY:
        switch (((NMHDR*) lParam)->idFrom) {
        case IDC_FAN_LIST:
            switch (((NMHDR*) lParam)->code) {
            case LVN_ITEMCHANGED:
            {
                NMLISTVIEW* lPoint = (LPNMLISTVIEW) lParam;
                if (lPoint->uNewState & LVIS_FOCUSED) {
                    // Select other item...
                    if (lPoint->iItem != -1) {
                        // Select other fan....
                        conf->fan_conf->lastSelectedFan = lPoint->iItem;

                        // Redraw fans
                        SendMessage(fanWindow, WM_PAINT, 0, 0);
                    }
                }
                if (lPoint->uNewState & 0x2000) { // checked, 0x1000 - unchecked
                    if (conf->fan_conf->lastSelectedSensor != -1) {
                        temp_block* sen = conf->fan_conf->FindSensor(conf->fan_conf->lastSelectedSensor);
                        if (!sen) { // add new sensor block
                            conf->fan_conf->lastProf->fanControls.push_back({ (short)conf->fan_conf->lastSelectedSensor });
                            sen = &conf->fan_conf->lastProf->fanControls.back();
                        }
                        if (!conf->fan_conf->FindFanBlock(sen, lPoint->iItem)) {
                            sen->fans.push_back({ (short)lPoint->iItem,{{0,0},{100,100}} });
                        }
                        SendMessage(fanWindow, WM_PAINT, 0, 0);
                    }
                }
                if (lPoint->uNewState & 0x1000 && lPoint->uOldState && 0x2000) { // unchecked
                    if (conf->fan_conf->lastSelectedSensor != -1) {
                        temp_block* sen = conf->fan_conf->FindSensor(conf->fan_conf->lastSelectedSensor);
                        if (sen) { // remove sensor block
                            for (auto iFan = sen->fans.begin(); iFan < sen->fans.end(); iFan++)
                                if (iFan->fanIndex == lPoint->iItem) {
                                    sen->fans.erase(iFan);
                                    break;
                                }
                            if (!sen->fans.size()) // remove sensor block!
                                for (auto iSen = conf->fan_conf->lastProf->fanControls.begin();
                                     iSen < conf->fan_conf->lastProf->fanControls.end(); iSen++)
                                    if (iSen->sensorIndex == sen->sensorIndex) {
                                        conf->fan_conf->lastProf->fanControls.erase(iSen);
                                        break;
                                    }
                        }
                        SendMessage(fanWindow, WM_PAINT, 0, 0);
                    }
                }
            } break;
            }
            break;
        case IDC_TEMP_LIST:
            switch (((NMHDR*) lParam)->code) {
            case LVN_ITEMCHANGED:
            {
                NMLISTVIEW* lPoint = (LPNMLISTVIEW) lParam;
                if (lPoint->uNewState & LVIS_FOCUSED) {
                    // Select other item...
                    if (lPoint->iItem != -1) {
                        // Select other fan....
                        conf->fan_conf->lastSelectedSensor = lPoint->iItem;
                        // Redraw fans
                        ReloadFanView(hDlg, conf->fan_conf->lastSelectedFan);
                        SendMessage(fanWindow, WM_PAINT, 0, 0);
                    }
                }
            } break;
            }
            break;
        }
        break;
    case WM_HSCROLL:
        switch (LOWORD(wParam)) {
        case TB_THUMBPOSITION: case TB_ENDTRACK: {
            if ((HWND)lParam == power_gpu) {
                conf->fan_conf->lastProf->GPUPower = (DWORD)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
                acpi->SetGPU(conf->fan_conf->lastProf->GPUPower);
            }
        } break;
        } break;
    case WM_DESTROY:
        //SetEvent(fuiEvent);
        //WaitForSingleObject(uiFanHandle, 1000);
        //CloseHandle(uiFanHandle);
        delete fanUIUpdate;
        LocalFree(sch_guid);
        break;
    }
    return 0;
}

void UpdateFanUI(LPVOID lpParam) {
    HWND tempList = GetDlgItem((HWND)lpParam, IDC_TEMP_LIST),
        fanList = GetDlgItem((HWND)lpParam, IDC_FAN_LIST),
        power_list = GetDlgItem((HWND)lpParam, IDC_COMBO_POWER);
    if (eve->mon && IsWindowVisible((HWND)lpParam)) {
        //DebugPrint("Fans UI update...\n");
        for (int i = 0; i < eve->mon->acpi->HowManySensors(); i++) {
            string name = to_string(eve->mon->senValues[i]) + " (" + to_string(eve->mon->maxTemps[i]) + ")";
            ListView_SetItemText(tempList, i, 0, (LPSTR)name.c_str());
        }
        for (int i = 0; i < eve->mon->acpi->HowManyFans(); i++) {
            string name = "Fan " + to_string(i + 1) + " (" + to_string(eve->mon->fanValues[i]) + ")";
            ListView_SetItemText(fanList, i, 0, (LPSTR)name.c_str());
        }
        SendMessage(fanWindow, WM_PAINT, 0, 0);
    }
}

//DWORD WINAPI UpdateFanUI(LPVOID lpParam) {
//    HWND tempList = GetDlgItem((HWND)lpParam, IDC_TEMP_LIST),
//        fanList = GetDlgItem((HWND)lpParam, IDC_FAN_LIST),
//        power_list = GetDlgItem((HWND)lpParam, IDC_COMBO_POWER);
//
//    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
//    while (WaitForSingleObject(fuiEvent, 250) == WAIT_TIMEOUT) {
//        if (eve->mon && IsWindowVisible((HWND)lpParam)) {
//            //DebugPrint("Fans UI update...\n");
//            for (int i = 0; i < eve->mon->acpi->HowManySensors(); i++) {
//                string name = to_string(eve->mon->senValues[i]) + " (" + to_string(eve->mon->maxTemps[i]) + ")";
//                ListView_SetItemText(tempList, i, 0, (LPSTR)name.c_str());
//            }
//            for (int i = 0; i < eve->mon->acpi->HowManyFans(); i++) {
//                string name = "Fan " + to_string(i + 1) + " (" + to_string(eve->mon->fanValues[i]) + ")";
//                ListView_SetItemText(fanList, i, 0, (LPSTR)name.c_str());
//            }
//            SendMessage(fanWindow, WM_PAINT, 0, 0);
//        }
//    }
//    return 0;
//}