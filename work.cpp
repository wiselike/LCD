#include <Windows.h>
#include <stdio.h>
#include "work.h"
#include "com.h"
#include "hwinfo.h"

CRITICAL_SECTION cs;
DWORD WINAPI MyThread(LPVOID lpParam);
HANDLE hWakeEvent = INVALID_HANDLE_VALUE;

volatile bool stopping = true;

Work::Work() {
	InitializeCriticalSection(&cs);  // 初始化临界区
};

Work::~Work () {
	// 结束线程
	stopping = true;
	SetEvent(hWakeEvent);
	WaitForSingleObject(hThread, INFINITE);
	CloseLCD();
	CloseHWiNFO();
	DeleteCriticalSection(&cs);  // 清理资源
}

VOID Work::working(LPCSTR COM) {
	LPVOID com = _strdup(COM);
	stopping = false;
	if (hWakeEvent!=INVALID_HANDLE_VALUE) CloseHandle(hWakeEvent);
	hWakeEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // 创建手动重置事件
	hThread = CreateThread(
        NULL,                   // 安全属性（默认）
        0,                      // 初始栈大小（默认）
        MyThread,               // 线程函数指针
        com,                    // 传递给线程的参数
        0,                      // 创建标志（0表示立即运行）
        NULL                    // 接收线程ID（可选）
    );

}

VOID Work::TryWorking(LPCSTR COM) {
	if (TryEnterCriticalSection(&cs))  // 尝试进入临界区，成功返回 TRUE，失败返回 FALSE
		working(COM);
	else
		printf("重复调用TryWorking\n");
}

DWORD WINAPI MyThread(LPVOID lpParam) {
    LPCSTR COM = (LPCSTR)lpParam;

	if (!InitLCD(COM)) {
		free(lpParam);
		LeaveCriticalSection(&cs);
		return 0;
	}

	TCHAR hwinfo_msg[100]={0};
	while (1) {
		if (WaitForSingleObject(hWakeEvent, 2000) != WAIT_TIMEOUT) {
			CloseLCD();
			CloseHandle(hWakeEvent);
			hWakeEvent = INVALID_HANDLE_VALUE;
			break;
		}
		if (stopping) { CloseLCD(); break; }
		if (!PrepareHWiNFOData()) {
			if (InitHWiNFO())
				PrepareHWiNFOData();
			else {
				SendLCDPacket("HELLO IGAME");
				continue;
			}
		}
		getHWiNFOPrint(hwinfo_msg, sizeof(hwinfo_msg));
		SendLCDPacket(hwinfo_msg);
	}

	free(lpParam);
	LeaveCriticalSection(&cs);
	return 0;
}
