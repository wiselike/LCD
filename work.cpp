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
	InitializeCriticalSection(&cs);  // ��ʼ���ٽ���
};

Work::~Work () {
	// �����߳�
	stopping = true;
	SetEvent(hWakeEvent);
	WaitForSingleObject(hThread, INFINITE);
	CloseLCD();
	CloseHWiNFO();
	DeleteCriticalSection(&cs);  // ������Դ
}

VOID Work::working(LPCSTR COM) {
	LPVOID com = _strdup(COM);
	stopping = false;
	if (hWakeEvent!=INVALID_HANDLE_VALUE) CloseHandle(hWakeEvent);
	hWakeEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // �����ֶ������¼�
	hThread = CreateThread(
        NULL,                   // ��ȫ���ԣ�Ĭ�ϣ�
        0,                      // ��ʼջ��С��Ĭ�ϣ�
        MyThread,               // �̺߳���ָ��
        com,                    // ���ݸ��̵߳Ĳ���
        0,                      // ������־��0��ʾ�������У�
        NULL                    // �����߳�ID����ѡ��
    );

}

VOID Work::TryWorking(LPCSTR COM) {
	if (TryEnterCriticalSection(&cs))  // ���Խ����ٽ������ɹ����� TRUE��ʧ�ܷ��� FALSE
		working(COM);
	else
		printf("�ظ�����TryWorking\n");
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
