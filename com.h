#include <Windows.h>

bool InitLCD(LPCSTR path);

void CloseLCD();

bool SendLCDPacket(LPCSTR buf);

