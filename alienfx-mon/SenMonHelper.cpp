#include "SenMonHelper.h"
#include "common.h"

#pragma comment(lib, "pdh.lib")

// debug print
#ifdef _DEBUG
#define DebugPrint(_x_) OutputDebugString(string(_x_).c_str());
#else
#define DebugPrint(_x_)
#endif

DWORD WINAPI UpdateSensors(LPVOID);
DWORD WINAPI CBiosProc(LPVOID);

extern ConfigMon* conf;
extern AlienFan_SDK::Control* acpi;

SenMonHelper::SenMonHelper()
{
	// Set data source...
	PdhSetDefaultRealTimeDataSource(DATA_SOURCE_WBEM);
	ModifyMon();

}

SenMonHelper::~SenMonHelper()
{
	if (hQuery)
		PdhCloseQuery(hQuery);
	if (acpi)
		delete acpi;
	delete[] counterValues;
}

void SenMonHelper::ModifyMon()
{
	if ((conf->bSensors || conf->eSensors) && !EvaluteToAdmin()) {
		conf->bSensors = conf->eSensors = false;
	}
	if ((conf->wSensors || conf->eSensors) && (hQuery || PdhOpenQuery(NULL, 0, &hQuery) == ERROR_SUCCESS)) {
		if (conf->wSensors) {
			PdhAddCounter(hQuery, COUNTER_PATH_CPU, 0, &hCPUCounter);
			PdhAddCounter(hQuery, COUNTER_PATH_HDD, 0, &hHDDCounter);
			PdhAddCounter(hQuery, COUNTER_PATH_GPU, 0, &hGPUCounter);
			PdhAddCounter(hQuery, COUNTER_PATH_TEMP, 0, &hTempCounter);
		}
		else {
			PdhRemoveCounter(hCPUCounter);
			PdhRemoveCounter(hHDDCounter);
			PdhRemoveCounter(hGPUCounter);
			PdhRemoveCounter(hTempCounter);
		}
		if (conf->eSensors) {
			PdhAddCounter(hQuery, COUNTER_PATH_ESIF, 0, &hTempCounter2);
			PdhAddCounter(hQuery, COUNTER_PATH_PWR, 0, &hPwrCounter);
		}
		else {
			PdhRemoveCounter(hTempCounter2);
			PdhRemoveCounter(hPwrCounter);
		}
		DebugPrint("PDH query on.\n");
		// Bugfix for incorrect value order
		PdhCollectQueryData(hQuery);
	} else
		if (hQuery) {
			DebugPrint("PDH query off.\n");
			PdhCloseQuery(hQuery);
			hQuery = NULL;
		}
	if (conf->bSensors) {
		if (!acpi) {
			acpi = new AlienFan_SDK::Control();
			if (!(conf->bSensors = acpi->Probe())) {
				delete acpi;
				acpi = NULL;
			}
		}
	}
	conf->needFullUpdate = true;
}

//void SenMonHelper::AddSensor(SENID sid, long val, string name) {
//	SENSOR* sen = conf->FindSensor(sid.sid);
//	if (!sen) {
//		// add sensor
//		conf->active_sensors[sid.sid] = { name, val, val, val, true };
//	}
//	else
//		if (sen->sname.empty()) {
//			// Update name
//			sen->sname = name;
//		}
//	conf->needFullUpdate = true;
//}

SENSOR* SenMonHelper::UpdateSensor(SENID sid, long val) {
	if (val > 10000 || val <= NO_SEN_VALUE) return NULL;

	SENSOR* sen = &conf->active_sensors[sid.sid];
	sen->min = sen->min <= NO_SEN_VALUE ? val : min(sen->min, val);
	if (sen->changed = (sen->min > NO_SEN_VALUE && sen->cur != val)) {
		sen->max = max(sen->max, val);
		if (sen->alarm || sen->highlight) {
			bool alarming = sen->direction ? val < sen->ap : val >= sen->ap;
			if (sen->alarm && !sen->alarming && alarming && sen->sname.size())
				ShowNotification(&conf->niData, "Alarm triggered!", "Sensor \"" + sen->sname +
					"\" " + (sen->direction ? "lower " : "higher ") + to_string(sen->ap) + " (Current: " + to_string(val) + ")!", true);
			sen->alarming = alarming;
		}
		sen->cur = val;
	}
	return sen->sname.empty() ? sen : NULL;
}

int SenMonHelper::GetValuesArray(HCOUNTER counter) {
	PDH_STATUS pdhStatus;
	DWORD count;
	while ((pdhStatus = PdhGetFormattedCounterArray(counter, PDH_FMT_LONG /*| PDH_FMT_NOSCALE*/, &counterSize, &count, counterValues)) == PDH_MORE_DATA) {
		delete[] counterValues;
		counterValues = new PDH_FMT_COUNTERVALUE_ITEM[(counterSize / sizeof(PDH_FMT_COUNTERVALUE_ITEM)) + 1];
	}

	if (pdhStatus != ERROR_SUCCESS) {
		return 0;
	}
	return count;
}

void SenMonHelper::UpdateSensors()
{
	PDH_FMT_COUNTERVALUE cCPUVal, cHDDVal;
	SENSOR* sen;

	if (conf->wSensors || conf->eSensors)
		// get indicators...
		PdhCollectQueryData(hQuery);

	if (conf->wSensors) { // group 0
		if (PdhGetFormattedCounterValue(hCPUCounter, PDH_FMT_LONG, &cType, &cCPUVal) == ERROR_SUCCESS && // CPU, code 0
			(sen = UpdateSensor({ 0, 0, 0 }, cCPUVal.longValue)))
			sen->sname = "CPU load";

		if (GlobalMemoryStatusEx(&memStat) && // RAM, code 1
		   (sen = UpdateSensor({ 0, 1, 0 }, memStat.dwMemoryLoad)))
			sen->sname = "Memory usage";

		if (PdhGetFormattedCounterValue(hHDDCounter, PDH_FMT_LONG, &cType, &cHDDVal) == ERROR_SUCCESS && // HDD, code 2
			(sen = UpdateSensor({ 0, 2, 0 }, 99 - cHDDVal.longValue)))
			sen->sname = "HDD load";

		if (GetSystemPowerStatus(&state) && // Battery, code 3
			(sen = UpdateSensor({ 0, 3, 0 }, state.BatteryLifePercent)))
			sen->sname = "Battery";

		valCount = GetValuesArray(hGPUCounter); // GPU, code 4
		long valLast = 0;
		for (unsigned i = 0; i < valCount && counterValues[i].szName != NULL; i++) {
			if ((counterValues[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA))
				valLast = max(valLast, counterValues[i].FmtValue.longValue);
		}
		if (sen = UpdateSensor({ 0, 4, 0 }, valLast))
			sen->sname = "GPU load";

		valCount = GetValuesArray(hTempCounter); // Temps, code 5
		for (WORD i = 0; i < valCount; i++) {
			if (counterValues[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA &&
				(sen = UpdateSensor({ i, 5, 0 }, counterValues[i].FmtValue.longValue - 273)))
				sen->sname = counterValues[i].szName;
		}
	}

	if (conf->bSensors) { // group 2
		// BIOS/WMI temperatures
		for (WORD i = 0; i < acpi->sensors.size(); i++) { // BIOS temps, code 0
			if (sen = UpdateSensor({ acpi->sensors[i].sid, 0, 2 }, acpi->GetTempValue(i)))
				sen->sname = conf->fan_conf.GetSensorName(&acpi->sensors[i]);
		}
		// Fan data
		for (WORD i = 0; i < acpi->fans.size(); i++) { // BIOS fans, code 1-3
			if (sen = UpdateSensor({ i, 1, 2 }, acpi->GetFanRPM(i)))
				sen->sname = "Fan " + to_string(i + 1) + " RPM";
			if (sen = UpdateSensor({ i, 2, 2 }, acpi->GetFanPercent(i)))
				sen->sname = "Fan " + to_string(i + 1) + " percent";
			if (sen = UpdateSensor({ i, 3, 2 }, acpi->GetFanBoost(i)))
				sen->sname = "Fan " + to_string(i + 1) + " boost";
		}
	} else
		if (conf->eSensors) {
			// ESIF temperatures...
			valCount = GetValuesArray(hTempCounter2); // Esif temps, code 0
			for (WORD i = 0; i < valCount; i++) {
				if ((counterValues[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) && counterValues[i].FmtValue.longValue &&
					(sen = UpdateSensor({ i, 0, 1 }, counterValues[i].FmtValue.longValue)))
					sen->sname = "ESIF " + to_string(i + 1);
			}
		}

	if (conf->eSensors) { // group 1
		// ESIF temperatures and power
		valCount = GetValuesArray(hPwrCounter); // Esif powers, code 1
		for (WORD i = 0; i < valCount; i++) {
			if (counterValues[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA &&
				(sen = UpdateSensor({ i, 1, 1 }, counterValues[i].FmtValue.longValue / 10)))
				sen->sname = "ESIF Power " + to_string(i + 1);
		}
	}
}