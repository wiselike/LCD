#include <Windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


HANDLE hCom = INVALID_HANDLE_VALUE;


// 16 λ�ۼ�У�飨SumCheck��
uint16_t CalcSumCheck(const BYTE* data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    // ֻ������ 16 λ
    return (uint16_t)(sum & 0xFFFF);
}

//5a 9f a9 00 0e dd 22 07 ea 04 0d 01 16 0f 3b 01 00 // 04 13, ʵ�ʹ�����19�ֽ�
BYTE* PackLCDPacket( const SYSTEMTIME*  st, const char* text, BYTE cmd, DWORD* outLen) {
    if (!st || !text || !outLen) return NULL;

    const size_t TIME_LEN = 10;            // ��(2)+��(1)+��(1)����(1)+ʱ(1)+��(1)+��(1)
    const size_t STR_LEN  = strlen(text) + 1;  // �ı� +0x00
    uint16_t payloadLen   = (uint16_t)(TIME_LEN + 1 + STR_LEN + 1 + 2); // 1λcmd+2λSumCheck
    uint16_t totalLen     = (uint16_t)(5 + payloadLen);
    BYTE* buf = (BYTE*)malloc(totalLen);
    if (!buf) return NULL;

    size_t idx = 0;
    // === ֡ͷ (5) ===
    buf[idx++] = 0x5A;
    buf[idx++] = 0x9F;
    buf[idx++] = 0xA9;
    buf[idx++] = 0x00;
    buf[idx++] = (BYTE)( payloadLen       & 0xFF);

    // === ʱ��� (10) ===
    buf[idx++] = 0xDD;
    buf[idx++] = 0x22;
    buf[idx++] = (BYTE)((st->wYear >> 8) & 0xFF);
    buf[idx++] = (BYTE)( st->wYear       & 0xFF);
    buf[idx++] = (BYTE)( st->wMonth);
    buf[idx++] = (BYTE)( st->wDay);
	buf[idx++] = (BYTE)( st->wDayOfWeek); // ����
    buf[idx++] = (BYTE)( st->wHour);
    buf[idx++] = (BYTE)( st->wMinute);
    buf[idx++] = (BYTE)( st->wSecond);
    
    // === �ַ����� ===
	buf[idx++] = 0x01;
	if (text!=NULL){
		memcpy(buf + idx, text, STR_LEN);
		idx += STR_LEN;
	}
    buf[idx++] = cmd;

    // === SumCheck ===
	uint16_t cs = CalcSumCheck(buf, idx);
    buf[idx++] = (BYTE)((cs >> 8) & 0xFF);  // ���ֽ�
    buf[idx++] = (BYTE)( cs       & 0xFF);  // ���ֽ�

    // ����ܳ���
    *outLen = totalLen;
    return buf;
}

void CloseLCD() {
	if (hCom!=INVALID_HANDLE_VALUE) CloseHandle(hCom);
	hCom = INVALID_HANDLE_VALUE;
}

bool InitLCD(LPCSTR path) {
	hCom = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    if (hCom == INVALID_HANDLE_VALUE) {
        printf("�򿪴���ʧ��\n");
        return false;
    }

    // ���ô��ڲ��� (115200 ������, 8 ����λ, ��У��, 1 ֹͣλΪ��)
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hCom, &dcb)) {
		printf("Error getting current serial parameters\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
	}
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(hCom, &dcb)) {
        printf("Error setting serial parameters\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

    // ���ó�ʱ
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hCom, &timeouts)) {
        printf("Error setting timeouts\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

    // ���͵�һ������
	BYTE sendBuf1[] = {0x5A, 0x9F, 0xA9, 0x00, 0x04, 0xE0, 0x1F, 0x02, 0xA5};
    DWORD bytesWritten;
    if (!WriteFile(hCom, sendBuf1, sizeof(sendBuf1), &bytesWritten, NULL)) {
        printf("���͵�һ������ʧ��\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

    // �ȴ�����������
    BYTE recvBuf[256] = {0};
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    const DWORD expectedLen = 20; // 0x0c + ǰ���ֽ�ͷ��һ�� 14 �ֽ�
    while (totalRead < expectedLen) {
        if (!ReadFile(hCom, recvBuf + totalRead, expectedLen - totalRead, &bytesRead, NULL)) {
            printf("���յ�һ������ʧ��\n");
            CloseHandle(hCom);
			hCom = INVALID_HANDLE_VALUE;
			return false;
        }
        if (bytesRead == 0) {
            // ��ʱ������
            break;
        }
        totalRead += bytesRead;
    }
	
    if (totalRead > 0) {
        printf("Received %d bytes: ", totalRead);
        for (DWORD i = 0; i < totalRead; ++i) {
            printf("%02X ", recvBuf[i]);
        }
        printf("\n");
    } else {
        printf("No data received\n");
    }
	
	//5A 9F A9 00 0C E0 1F 0A 00 05 0D 00 00 01 00 02 CA
	// У���Ƿ�ΪԤ�ڵ�����
    BYTE expectedBuf[] = {0x5A, 0x9F, 0xA9, 0x00, 0x0C, 0xE0, 0x1F, 0x0A, 0x00, 0x05, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x02, 0xC9};
    if (totalRead == sizeof(expectedBuf) && memcmp(recvBuf, expectedBuf, sizeof(expectedBuf)) == 0) {
        printf("Response matches expected packet.\n");
    } else {
        printf("Response does NOT match expected packet.\n");
    }

    // �ڶ��η��͵�����
    BYTE sendBuf2[] = {0x5A, 0x9F, 0xA9, 0x00, 0x05, 0xEC, 0x13, 0x01, 0x02, 0xA7};
    if (!WriteFile(hCom, sendBuf2, sizeof(sendBuf2), &bytesWritten, NULL)) {
        printf("�ڶ��η���ʧ��\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

	// �����η��͵�����
    BYTE sendBuf3[] = {0x5A, 0x9F, 0xA9, 0x00, 0x05, 0xEB, 0x14, 0x00, 0x02, 0xA6};
    if (!WriteFile(hCom, sendBuf3, sizeof(sendBuf3), &bytesWritten, NULL)) {
        printf("�����η���ʧ��\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

	// ���Ĵη��͵�����
    BYTE sendBuf4[] = {0x5A, 0x9F, 0xA9, 0x00, 0x06, 0xE1, 0x1E, 0x64, 0x00, 0x03, 0x0B};
    if (!WriteFile(hCom, sendBuf4, sizeof(sendBuf4), &bytesWritten, NULL)) {
        printf("���Ĵη���ʧ��\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

	// ����η��͵�����
    BYTE sendBuf5[] = {0x5A, 0x9F, 0xA9, 0x00, 0x06, 0xE1, 0x1E, 0x0A, 0x00, 0x02, 0xB1};
    if (!WriteFile(hCom, sendBuf5, sizeof(sendBuf5), &bytesWritten, NULL)) {
        printf("����η���ʧ��\n");
        CloseHandle(hCom);
		hCom = INVALID_HANDLE_VALUE;
		return false;
    }

	// �����η��͵�����
	SYSTEMTIME st;
    GetLocalTime(&st);
	DWORD len = 0;
	BYTE cmd[]={0x00, 0x02};
	BYTE* packet = PackLCDPacket(&st, "", 2, &len);
	printf("�����η������� (%d �ֽ�):\n", len);
    for (DWORD i = 0; i < len; i++) {
        printf("%02x ", packet[i]);
    }
    printf("\n");
    if (packet) {
        if (!WriteFile(hCom, packet, len, &bytesWritten, NULL)) {
			printf("�����η���ʧ��\n");
			CloseHandle(hCom);
			hCom = INVALID_HANDLE_VALUE;
			return false;
		}
        free(packet);
    }

	return true;
}

bool SendLCDPacket(LPCSTR buf) {
	static SYSTEMTIME st;
	// ��n�η��͵�����
    GetLocalTime(&st);
	DWORD len=0, bytesWritten=0;
	// BYTE* packet = PackLCDPacket(&st, "C57 G49 R541 T57.1 T44.5 T23.8", 2, &len);
	BYTE* packet = PackLCDPacket(&st, "GEFORCE RTX 5080", 2, &len);
	printf("�������� (%d �ֽ�):\n", len);
    for (DWORD i = 0; i < len; i++) {
        printf("%02x ", packet[i]);
    }
    printf("\n");
    if (hCom!=INVALID_HANDLE_VALUE && packet) {
        if (!WriteFile(hCom, packet, len, &bytesWritten, NULL)) {
			printf("��n�η���ʧ��\n");
			CloseHandle(hCom);
			hCom = INVALID_HANDLE_VALUE;
			return false;
		} else {
			printf("��n�η��ͳɹ�������%d�ֽ�\n", bytesWritten);
		}
        free(packet);
    }
	return true;
}
