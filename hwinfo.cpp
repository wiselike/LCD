#include <windows.h>
#include <stdio.h>
#include "main.h"

#pragma pack(push, 1)
struct HWINFO_SENSORS_READING
{
	unsigned int readingType;
	unsigned int sensorIndex;
	unsigned int readingId;
	char labelOriginal[128];
	char labelUser[128];
	char unit[16];
	double value;
	double valueMin;
	double valueMax;
	double valueAvg;
};
struct HWINFO_SENSORS_SENSOR
{
	unsigned int sensorId;
	unsigned int sensorInst;
	char sensorNameOriginal[128];
	char sensorNameUser[128];
};
struct HWINFO_SENSORS_SHARED_MEM2
{
	unsigned int signature;
	unsigned int version;
	unsigned int revision;
	long long int pollTime;
	unsigned int sensorOffset;
	unsigned int sensorSize;
	unsigned int sensorCount;
	unsigned int readingOffset;
	unsigned int readingSize;
	unsigned int readingCount;
};
#pragma pack(pop)

HANDLE hMutex = nullptr;
HANDLE hMap = nullptr;
LPVOID mapAddress = nullptr;
DWORD restart_count=0;


typedef struct SensorBlock {
    double cpu; // Total CPU Utility
    double gpu; // GPU Core Load
	double rpm;	// Chassis5
    double t1;	// CPU Die (average)
	double t2;	// T_Sensor

} SensorBlock;

SensorBlock gSensorData = {-1,-1,-1,-1,-1};
static bool enable1=false;
static bool enable2=false;
static bool enable3=false;
static bool enable4=false;
static bool enable5=false;

void CloseHWiNFO() {
	if (hMutex!=nullptr) CloseHandle(hMutex);
	if (mapAddress!=nullptr) UnmapViewOfFile(mapAddress);
	if (hMap!=nullptr) CloseHandle(hMap);
	hMap = mapAddress = hMutex = nullptr;
}

bool InitHWiNFO() {
	CloseHWiNFO();
	enable1=false;
	enable2=false;
	enable3=false;
	enable4=false;
	enable5=false;

	if (restart_count++ > 60) PostMessage(hWnd, WM_CLOSE, 0, 0); // 60次都没起来，进程就退出了吧

	hMutex = OpenMutex(SYNCHRONIZE,        // 只需同步权
                               FALSE,              // 句柄不可继承
                               "Global\\HWiNFO_SM2_MUTEX");
	if (!hMutex)
    {
        printf("OpenMutex failed (%lu)\n", GetLastError());
        return false;          // HWiNFO 未运行或共享内存未启用
    }

	hMap = OpenFileMapping(FILE_MAP_READ, false, "Global\\HWiNFO_SENS_SM2");
	if (!hMap)
    {
        printf("OpenMapFile failed (%lu)\n", GetLastError());
        return false;          // HWiNFO 未运行或共享内存未启用
    }

	mapAddress = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (!mapAddress)
    {
        printf("MapFileView failed (%lu)\n", GetLastError());
        return false;          // HWiNFO 未运行或共享内存未启用
    }

	return true;
}

bool PrepareHWiNFOData() {
	static double* data1 = nullptr;
	static LPCSTR check1 = nullptr;

	static double* data2 = nullptr;
	static LPCSTR check2 = nullptr;

	static double* data3 = nullptr;
	static LPCSTR check3 = nullptr;

	static double* data4 = nullptr;
	static LPCSTR check4 = nullptr;

	static double* data5 = nullptr;
	static LPCSTR check5 = nullptr;

	int fast_read_count=0;
	DWORD wait = WaitForSingleObject(hMutex, 5000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)   // 包括放弃互斥体的情形
    {
        CloseHandle(hMutex);
		hMutex = nullptr;
        return false;
    }

	if (enable1 && check1!=0)
		if (strncmp(check1, "Total CPU Utility", strlen("Total CPU Utility")+1)==0) {
			gSensorData.cpu = *data1;
			fast_read_count++;
		}
		else {
			gSensorData.cpu = -1;
			enable1 = false;
			goto READFULL;
		}


	if (enable2 && check2!=0)
		if (strncmp(check2, "GPU Core Load", strlen("GPU Core Load")+1)==0) {
			gSensorData.gpu = *data2;
			fast_read_count++;
		}
		else {
			gSensorData.gpu = -1;
			enable2 = false;
			goto READFULL;
		}

	if (enable3 && check3!=0)
		if (strncmp(check3, "Chassis5", strlen("Chassis5")+1)==0) {
			gSensorData.rpm = *data3;
			fast_read_count++;
		}
		else {
			gSensorData.rpm = -1;
			enable3 = false;
			goto READFULL;
		}

	if (enable4 && check4!=0)
		if (strncmp(check4, "CPU Die (average)", strlen("CPU Die (average)")+1)==0) {
			gSensorData.t1 = *data4;
			fast_read_count++;
		}
		else {
			gSensorData.t1 = -1;
			enable4 = false;
			goto READFULL;
		}

	if (enable5 && check5!=0)
		if (strncmp(check5, "T_Sensor", strlen("T_Sensor")+1)==0) {
			gSensorData.t2 = *data5;
			fast_read_count++;
		}
		else {
			gSensorData.t2 = -1;
			enable5 = false;
			goto READFULL;
		}

	if (fast_read_count>0) {
		ReleaseMutex(hMutex);
		return true;
	}

READFULL:
	void *readings = nullptr;
	HWINFO_SENSORS_SHARED_MEM2 hwinfo = {0};
	memcpy(&hwinfo, mapAddress, sizeof(HWINFO_SENSORS_SHARED_MEM2));
	readings = malloc(hwinfo.readingSize * hwinfo.readingCount);
	memcpy(readings, (unsigned char*)mapAddress + hwinfo.readingOffset, hwinfo.readingSize * hwinfo.readingCount);
	ReleaseMutex(hMutex);

	int slow_read_count = 0;
	for (unsigned int i = 0; i < hwinfo.readingCount; ++i)
	{
		HWINFO_SENSORS_READING *reading = (HWINFO_SENSORS_READING*) ((unsigned char*) readings + hwinfo.readingSize * i);
		if (strncmp(reading->labelOriginal, "Total CPU Utility", strlen("Total CPU Utility")+1)==0) {
			enable1 = true;
			check1 = (char*)mapAddress + hwinfo.readingOffset + ((char*)&reading->labelOriginal - (char*)readings);
			data1 = (double*)((char*)mapAddress + hwinfo.readingOffset+((char*)&reading->value - (char*)readings));
			gSensorData.cpu = reading->value;
			slow_read_count++;
		}

		if (strncmp(reading->labelOriginal, "GPU Core Load", strlen("GPU Core Load")+1)==0) {
			enable2 = true;
			check2 = (char*)mapAddress + hwinfo.readingOffset + ((char*)&reading->labelOriginal - (char*)readings);
			data2 = (double*)((char*)mapAddress + hwinfo.readingOffset+((char*)&reading->value - (char*)readings));
			gSensorData.gpu = reading->value;
			slow_read_count++;
		}

		if (strncmp(reading->labelOriginal, "Chassis5", strlen("Chassis5")+1)==0) {
			enable3 = true;
			check3 = (char*)mapAddress + hwinfo.readingOffset + ((char*)&reading->labelOriginal - (char*)readings);
			data3 = (double*)((char*)mapAddress + hwinfo.readingOffset+((char*)&reading->value - (char*)readings));
			gSensorData.rpm = reading->value;
			slow_read_count++;
		}

		if (strncmp(reading->labelOriginal, "CPU Die (average)", strlen("CPU Die (average)")+1)==0) {
			enable4 = true;
			check4 = (char*)mapAddress + hwinfo.readingOffset + ((char*)&reading->labelOriginal - (char*)readings);
			data4 = (double*)((char*)mapAddress + hwinfo.readingOffset+((char*)&reading->value - (char*)readings));
			gSensorData.t1 = reading->value;
			slow_read_count++;
		}

		if (strncmp(reading->labelOriginal, "T_Sensor", strlen("T_Sensor")+1)==0) {
			enable5 = true;
			check5 = (char*)mapAddress + hwinfo.readingOffset + ((char*)&reading->labelOriginal - (char*)readings);
			data5 = (double*)((char*)mapAddress + hwinfo.readingOffset+((char*)&reading->value - (char*)readings));
			gSensorData.t2 = reading->value;
			slow_read_count++;
		}
	}

	if (readings) free(readings);
	
	if (slow_read_count<1) return false;
	return true;
}

void formatFloatCPU(char *buf, double v) {
	if (v<0) {
		sprintf_s(buf, 3, "FF");
		return;
	}
	if (v>99.9999) {
		sprintf_s(buf, 3, "FF");
		return;
	}
	sprintf_s(buf, 6, "%.1lf", v);
	return;
}

void getHWiNFOPrint(char *hwinfo_msg, int maxlen) {
	char cpu[7]={0};
	formatFloatCPU(cpu, gSensorData.cpu);
	sprintf_s(hwinfo_msg, maxlen, "C %s G %d R%d T%.1lf T%d", cpu, (int)gSensorData.gpu, (int)gSensorData.rpm, gSensorData.t1, int(gSensorData.t2));
	// sprintf_s(hwinfo_msg, maxlen, "C %s G %d R%d T%.1lf", cpu, (int)gSensorData.gpu, (int)gSensorData.rpm, gSensorData.t1);
	
	printf("CPU %lf, GPU %lf, RPM %lf, T1 %lf, T2 %lf\n", gSensorData.cpu, gSensorData.gpu, gSensorData.rpm, gSensorData.t1, gSensorData.t2);
}
