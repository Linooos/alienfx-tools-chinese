#include "ConfigHandler.h"
#include "resource.h"
#include <Shlwapi.h>

// debug print
#ifdef _DEBUG
#define DebugPrint(_x_) OutputDebugString(_x_);
#else
#define DebugPrint(_x_)
#endif

ConfigHandler::ConfigHandler() {

	RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Alienfxgui"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey1, NULL);
	RegCreateKeyEx(hKey1, TEXT("Events"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey3, NULL);
	RegCreateKeyEx(hKey1, TEXT("Profiles"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey4, NULL);

}

ConfigHandler::~ConfigHandler() {
	Save();
	if (fan_conf) delete fan_conf;
	if (amb_conf) delete amb_conf;
	if (hap_conf) delete hap_conf;
	RegCloseKey(hKey1);
	RegCloseKey(hKey3);
	RegCloseKey(hKey4);
}

void ConfigHandler::updateProfileByID(unsigned id, std::string name, std::string app, DWORD flags, DWORD* eff) {
	profile* prof = FindProfile(id);
	if (!prof) {
		// New profile
		prof = new profile{id};
		profiles.push_back(prof);
	}
	// update profile..
	if (!name.empty())
		prof->name = name;
	if (!app.empty())
		prof->triggerapp.push_back(app);
	if (flags != -1) {
		prof->flags = LOWORD(flags);
		prof->effmode = HIWORD(flags);
	}
	if (eff) {
		prof->globalEffect = (byte) LOWORD(eff[0]);
		prof->globalDelay = (byte) HIWORD(eff[0]);
		prof->effColor1.ci = eff[1];
		prof->effColor2.ci = eff[2];
	}
}

void ConfigHandler::updateProfileFansByID(unsigned id, unsigned senID, fan_block* temp, DWORD flags) {

	profile* prof = FindProfile(id);
	if (prof) {
		if (temp) {
			for (int i = 0; i < prof->fansets.fanControls.size(); i++)
				if (prof->fansets.fanControls[i].sensorIndex == senID) {
					prof->fansets.fanControls[i].fans.push_back(*temp);
					return;
				}
			prof->fansets.fanControls.push_back({(short) senID, {*temp}});
		}
		if (flags != -1) {
			prof->fansets.powerStage = LOWORD(flags);
			prof->fansets.GPUPower = HIWORD(flags);
		}
		return;
	} else {
		prof = new profile{id};
		if (temp) {
			prof->fansets.fanControls.push_back({(short) senID, {*temp}});
		}
		if (flags != -1) {
			prof->fansets.powerStage = LOWORD(flags);
			prof->fansets.GPUPower = HIWORD(flags);
		}
		profiles.push_back(prof);
	}
}

profile* ConfigHandler::FindProfile(int id) {
	profile* prof = NULL;
	for (int i = 0; i < profiles.size(); i++)
		if (profiles[i]->id == id) {
			prof = profiles[i];
			break;
		}
	return prof;
}

profile* ConfigHandler::FindProfileByApp(string appName, bool active)
{
	for (int j = 0; j < profiles.size(); j++)
		if (active || !(profiles[j]->flags & PROF_ACTIVE)) {
			for (int i = 0; i < profiles[j]->triggerapp.size(); i++)
				if (profiles[j]->triggerapp[i] == appName)
					// app is belong to profile!
					return profiles[j];
		}
	return NULL;
}

bool ConfigHandler::IsPriorityProfile(profile* prof) {
	return prof && prof->flags & PROF_PRIORITY;
}

void ConfigHandler::SetStates() {
	bool oldStateOn = stateOn, oldStateDim = stateDimmed;
	// Lights on state...
	stateOn = lightsOn && stateScreen;
	// Dim state...
	stateDimmed = IsDimmed() || dimmedScreen || (dimmedBatt && !statePower);
	if (oldStateOn != stateOn || oldStateDim != stateDimmed) {
		SetIconState();
		Shell_NotifyIcon(NIM_MODIFY, &niData);
	}

	finalBrightness = (byte) (stateOn ? stateDimmed ? 255 - dimmingPower : 255 : 0);
	finalPBState = finalBrightness > 0 ? (byte) dimPowerButton : (byte) offPowerButton;
}

void ConfigHandler::SetIconState() {
	// change tray icon...
	niData.hIcon = (HICON)LoadImage(GetModuleHandle(NULL),
						stateOn ? stateDimmed ? MAKEINTRESOURCE(IDI_ALIENFX_DIM) : MAKEINTRESOURCE(IDI_ALIENFX_ON) : MAKEINTRESOURCE(IDI_ALIENFX_OFF),
						IMAGE_ICON,	GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
}

bool ConfigHandler::IsDimmed() {
	return dimmed || (!activeProfile->ignoreDimming && activeProfile->flags & PROF_DIMMED);
}

void ConfigHandler::SetDimmed() {
	if (activeProfile->flags & PROF_DIMMED) {
		if (!dimmed && !activeProfile->ignoreDimming) {
			activeProfile->ignoreDimming = true;
			return;
		}
		if (!dimmed || !activeProfile->ignoreDimming)
			activeProfile->ignoreDimming = !activeProfile->ignoreDimming;
	}

	dimmed = !dimmed;
}
int ConfigHandler::GetEffect() {
	return enableMon ? activeProfile->effmode : 3;
}

void ConfigHandler::GetReg(char *name, DWORD *value, DWORD defValue) {
	DWORD size = sizeof(DWORD);
	if (RegGetValueA(hKey1, NULL, name, RRF_RT_DWORD | RRF_ZEROONFAILURE, NULL, value, &size) != ERROR_SUCCESS)
		*value = defValue;
}

void ConfigHandler::SetReg(char *text, DWORD value) {
	RegSetValueEx( hKey1, text, 0, REG_DWORD, (BYTE*)&value, sizeof(DWORD) );
}

void ConfigHandler::Load() {
	DWORD size_c = 16*sizeof(DWORD);// 4 * 16
	DWORD activeProfileID = 0;

	GetReg("AutoStart", &startWindows);
	GetReg("Minimized", &startMinimized);
	GetReg("UpdateCheck", &updateCheck, 1);
	GetReg("LightsOn", &lightsOn, 1);
	GetReg("Dimmed", &dimmed);
	GetReg("Monitoring", &enableMon, 1);
	GetReg("GammaCorrection", &gammaCorrection, 1);
	GetReg("DisableAWCC", &awcc_disable);
	GetReg("ProfileAutoSwitch", &enableProf);
	GetReg("OffWithScreen", &offWithScreen);
	GetReg("NoDesktopSwitch", &noDesktop);
	GetReg("DimPower", &dimPowerButton);
	GetReg("DimmedOnBattery", &dimmedBatt);
	GetReg("ActiveProfile", &activeProfileID);
	GetReg("OffPowerButton", &offPowerButton);
	GetReg("EsifTemp", &esif_temp);
	GetReg("DimmingPower", &dimmingPower, 92);
	GetReg("FanControl", &fanControl);
	RegGetValue(hKey1, NULL, TEXT("CustomColors"), RRF_RT_REG_BINARY | RRF_ZEROONFAILURE, NULL, customColors, &size_c);

	int vindex = 0;
	char name[256];
	int pid = -1, appid = -1;
	LSTATUS ret = 0;
	// Profiles...
	do {
		DWORD len = 255, lend = 0;
		if ((ret = RegEnumValueA(hKey4, vindex, name, &len, NULL, NULL, NULL, &lend)) == ERROR_SUCCESS) {
			lend++; len++;
			BYTE *data = new BYTE[lend];
			RegEnumValueA(hKey4, vindex, name, &len, NULL, NULL, data, &lend);
			vindex++;
			if (sscanf_s(name, "Profile-%d", &pid) == 1) {
				updateProfileByID(pid, (char*)data, "", -1, NULL);
			}
			if (sscanf_s(name, "Profile-flags-%d", &pid) == 1) {
				updateProfileByID(pid, "", "", *(DWORD*)data, NULL);
			}
			if (sscanf_s(name, "Profile-app-%d-%d", &pid, &appid) == 2) {
				PathStripPath((char*)data);
				updateProfileByID(pid, "", (char*)data, -1, NULL);
			}
			int senid, fanid;
			if (sscanf_s(name, "Profile-fans-%d-%d-%d", &pid, &senid, &fanid) == 3) {
				// add fans...
				fan_block fan{(short) fanid};
				for (unsigned i = 0; i < lend; i += 2) {
					fan.points.push_back({data[i], data[i+1]});
				}
				updateProfileFansByID(pid, senid, &fan, -1);
			}
			if (sscanf_s(name, "Profile-effect-%d", &pid) == 1) {
				updateProfileByID(pid, "", "", -1, (DWORD*)data);
			}
			if (sscanf_s(name, "Profile-power-%d", &pid) == 1) {
				updateProfileFansByID(pid, -1, NULL, *(DWORD*)data);
			}
			delete[] data;
		}
	} while (ret == ERROR_SUCCESS);
	vindex = 0;
	// Event mappings...
	do {
		DWORD len = 255, lend = 0; lightset map;
		// get id(s)...
		if ((ret = RegEnumValueA( hKey3, vindex, name, &len, NULL, NULL, NULL, &lend )) == ERROR_SUCCESS) {
			BYTE *inarray = new BYTE[lend]; len++;
			RegEnumValueA(hKey3, vindex, name, &len, NULL, NULL, (LPBYTE) inarray, &lend);
			vindex++;
			if (sscanf_s((char*)name, "Set-%d-%d-%d", &map.devid, &map.lightid, &pid) == 3) {
				BYTE* inPos = inarray;
				for (int i = 0; i < 4; i++) {
					map.eve[i].fs.s = *((DWORD*)inPos);
					inPos += sizeof(DWORD);
					map.eve[i].source = *inPos;
					inPos++;
					BYTE mapSize = *inPos;
					inPos++;
					map.eve[i].map.clear();
					for (int j = 0; j < mapSize; j++) {
						map.eve[i].map.push_back(*((AlienFX_SDK::afx_act*)inPos));
						inPos += sizeof(AlienFX_SDK::afx_act);
					}
				}
				profile *prof = FindProfile(pid);
				if (prof) {
					prof->lightsets.push_back(map);
				}
			}
			delete[] inarray;
		}
	} while (ret == ERROR_SUCCESS);

	if (!profiles.size()) {
		// need new profile
		profile* prof = new profile({0, 1, 0, {}, "Default"});
		profiles.push_back(prof);
	}
	defaultProfile = profiles.front();
	if (profiles.size() == 1) {
		profiles.front()->flags |= PROF_DEFAULT;
	} else {
		// check for default profile
		for (auto iter = profiles.begin(); iter < profiles.end(); iter++)
			if ((*iter)->flags & PROF_DEFAULT) {
				defaultProfile = *iter;
				break;
			}
	}
	activeProfile = FindProfile(activeProfileID);
	if (!activeProfile) activeProfile = defaultProfile;
	active_set = &activeProfile->lightsets;
	stateDimmed = IsDimmed();
	stateOn = lightsOn;

	fan_conf = new ConfigFan();
	amb_conf = new ConfigAmbient();
	hap_conf = new ConfigHaptics();
}

void ConfigHandler::Save() {

	BYTE* out;

	if (fan_conf) fan_conf->Save();
	if (amb_conf) amb_conf->Save();
	if (hap_conf) hap_conf->Save();

	SetReg("AutoStart", startWindows);
	SetReg("Minimized", startMinimized);
	SetReg("UpdateCheck", updateCheck);
	SetReg("LightsOn", lightsOn);
	SetReg("Dimmed", dimmed);
	SetReg("Monitoring", enableMon);
	SetReg("OffWithScreen", offWithScreen);
	SetReg("NoDesktopSwitch", noDesktop);
	SetReg("DimPower", dimPowerButton);
	SetReg("DimmedOnBattery", dimmedBatt);
	SetReg("OffPowerButton", offPowerButton);
	SetReg("DimmingPower", dimmingPower);
	SetReg("ActiveProfile", activeProfile->id);
	SetReg("GammaCorrection", gammaCorrection);
	SetReg("ProfileAutoSwitch", enableProf);
	SetReg("DisableAWCC", awcc_disable);
	SetReg("EsifTemp", esif_temp);
	SetReg("FanControl", fanControl);

	RegSetValueEx( hKey1, TEXT("CustomColors"), 0, REG_BINARY, (BYTE*)customColors, sizeof(DWORD) * 16 );

	RegDeleteTreeA(hKey1, "Profiles");
	RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Alienfxgui\\Profiles"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey4, NULL);// &dwDisposition);

	RegDeleteTreeA(hKey1, "Events");
	RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("SOFTWARE\\Alienfxgui\\Events"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey3, NULL);// &dwDisposition);

	for (int j = 0; j < profiles.size(); j++) {
		string name = "Profile-" + to_string(profiles[j]->id);
		RegSetValueExA( hKey4, name.c_str(), 0, REG_SZ, (BYTE*)profiles[j]->name.c_str(), (DWORD)profiles[j]->name.length() );
		name = "Profile-flags-" + to_string(profiles[j]->id);
		DWORD flagset = MAKELONG(profiles[j]->flags, profiles[j]->effmode);
		RegSetValueExA( hKey4, name.c_str(), 0, REG_DWORD, (BYTE*)&flagset, sizeof(DWORD));

		for (int i = 0; i < profiles[j]->triggerapp.size(); i++) {
			name = "Profile-app-" + to_string(profiles[j]->id) + "-" + to_string(i);
			RegSetValueExA(hKey4, name.c_str(), 0, REG_SZ, (BYTE *) profiles[j]->triggerapp[i].c_str(), (DWORD) profiles[j]->triggerapp[i].length());
		}

		for (int i = 0; i < profiles[j]->lightsets.size(); i++) {
			//preparing name
			lightset cur = profiles[j]->lightsets[i];
			name = "Set-" + to_string(cur.devid) + "-" + to_string(cur.lightid) + "-" + to_string(profiles[j]->id);
			//preparing binary....
			size_t size = 4 * (sizeof(DWORD) + 2 * sizeof(BYTE));
			for (int j = 0; j < 4; j++)
				size += cur.eve[j].map.size() * (sizeof(AlienFX_SDK::afx_act));
			out = new BYTE[size];
			BYTE* outPos = out;
			for (int j = 0; j < 4; j++) {
				*((int*)outPos) = cur.eve[j].fs.s;
				outPos += 4;
				*outPos = cur.eve[j].source;
				outPos++;
				*outPos = (BYTE)cur.eve[j].map.size();
				outPos++;
				for (int k = 0; k < cur.eve[j].map.size(); k++) {
					memcpy(outPos, &cur.eve[j].map.at(k), sizeof(AlienFX_SDK::afx_act));
					outPos += sizeof(AlienFX_SDK::afx_act);
				}
			}
			RegSetValueExA( hKey3, name.c_str(), 0, REG_BINARY, (BYTE*)out, (DWORD)size);
			delete[] out;
		}
		// Global effects
		if (profiles[j]->flags & PROF_GLOBAL_EFFECTS) {
			DWORD buffer[3];
			name = "Profile-effect-" + to_string(profiles[j]->id);
			buffer[0] = MAKELONG(profiles[j]->globalEffect, profiles[j]->globalDelay);
			buffer[1] = profiles[j]->effColor1.ci;
			buffer[2] = profiles[j]->effColor2.ci;
			RegSetValueExA(hKey4, name.c_str(), 0, REG_BINARY, (BYTE*)buffer, 3*sizeof(DWORD));
		}
		// Fans....
		if (profiles[j]->flags & PROF_FANS) {
			// save powers..
			name = "Profile-power-" + to_string(profiles[j]->id);
			DWORD pvalue = MAKELONG(profiles[j]->fansets.powerStage, profiles[j]->fansets.GPUPower);
			RegSetValueExA(hKey4, name.c_str(), 0, REG_DWORD, (BYTE*)&pvalue, sizeof(DWORD));
			// save fans...
			for (int i = 0; i < profiles[j]->fansets.fanControls.size(); i++) {
				temp_block* sens = &profiles[j]->fansets.fanControls[i];
				for (int k = 0; k < sens->fans.size(); k++) {
					fan_block* fans = &sens->fans[k];
					name = "Profile-fans-" + to_string(profiles[j]->id) + "-" + to_string(sens->sensorIndex) + "-" + to_string(fans->fanIndex);
					byte* outdata = new byte[fans->points.size() * 2];
					for (int l = 0; l < fans->points.size(); l++) {
						outdata[2 * l] = (byte) fans->points[l].temp;
						outdata[(2 * l) + 1] = (byte) fans->points[l].boost;
					}

					RegSetValueExA( hKey4, name.c_str(), 0, REG_BINARY, (BYTE*) outdata, (DWORD) fans->points.size() * 2 );
					delete[] outdata;
				}
			}
		}
	}
}