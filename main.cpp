// lcd.cpp : ����Ӧ�ó������ڵ㡣
//

#include<Windows.h>
#include<stdio.h>
#include <dbt.h>        // WM_DEVICECHANGE / DEV_BROADCAST_*
#include <vector>
#include <string>
#include "work.h"

#ifdef _DEBUG
// Debug: ����̨��ϵͳ
#pragma comment(linker, "/SUBSYSTEM:CONSOLE /ENTRY:WinMainCRTStartup")
#else
// Release: ������ Windows GUI ��ϵͳ
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif


using namespace std;

LPSTR WND_NAME = "lcd-server";
HINSTANCE g_hInst = nullptr;
HWND hWnd = nullptr;
HDEVNOTIFY g_hCOMNotify = nullptr;
Work *work = nullptr;

void HotKey( HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam )
{
	//switch( wParam )
	{
	//default:
		{
			MessageBox( hWnd, "�������˳�!", "Succ", MB_OK|MB_TOPMOST );
			PostMessage(hWnd, WM_DESTROY, 0, 0);
		}
	}
}

HDEVNOTIFY registerListenCOM( HWND hWnd ) {
	// 1) ֻ���� GUID_DEVINTERFACE_COMPORT ���͵��豸֪ͨ
	DEV_BROADCAST_DEVICEINTERFACE di={0};
	di.dbcc_size       = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	di.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	di.dbcc_classguid  = GUID_DEVINTERFACE_COMPORT;

	HDEVNOTIFY notify = RegisterDeviceNotification( hWnd, &di, DEVICE_NOTIFY_WINDOW_HANDLE );
	if ( !notify ) {
		MessageBox( hWnd, "�޷�������COM�¼�!", "Fail", MB_OK|MB_TOPMOST );
	}
	return notify;
}

// ͨ���豸·��ֱ�ӻ�ȡ COM ����
LPSTR parseComName(LPCSTR devicePath)
{
	if (!devicePath || !*devicePath) return nullptr;
	char *path = _strdup(devicePath);

	// 1. ȥ��ǰ׺ "\\?\"
    char kPrefix[] = "\\\\?\\";
    if (strncmp(path, kPrefix, strlen(kPrefix)) == 0)
    {
        memmove(path, path + strlen(kPrefix), strlen(path) - strlen(kPrefix) + 1);
    }

    // 2. ȥ������ "#{GUID}"
    if (char* brace = strstr(path, "#{"))
        *brace = '\0';

    // 3. ������ '#' �滻�� '\\'
    for (char* c = path; *c; c++)
        if (*c == '#') *c = '\\';

    // 4. ƴ��ע�������·��
	string regPath = "SYSTEM\\CurrentControlSet\\Enum\\" + string(path) + "\\Device Parameters";
	free(path);

    // 5. �򿪼�����ȡ PortName
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return nullptr;

    CHAR portName[64] = {0};
    DWORD len = sizeof(portName);
    if (RegQueryValueEx(hKey, "PortName", nullptr, nullptr, reinterpret_cast<LPBYTE>(portName), &len) != ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return nullptr;
    }
    RegCloseKey(hKey);


	if (len>0) return _strdup(("\\\\.\\" + string(portName)).c_str());
	return nullptr;
}

LPSTR getNewCOMName(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)
	{
		PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
		if (pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
		{
			PDEV_BROADCAST_DEVICEINTERFACE pDev = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;

			if (wParam == DBT_DEVICEARRIVAL) { // �����¼�
				LPSTR comName = parseComName(pDev->dbcc_name);
				if (comName!=nullptr)
					MessageBox(hWnd, "��⵽��LCD����", "Succ", MB_OK | MB_ICONINFORMATION);
				else
					MessageBox(hWnd, "�޷���ȡCOM��", "Fail", MB_OK | MB_ICONINFORMATION);
				return comName;
			}
		}
	}
	return nullptr;
}

vector<string> enumerateExistingComPorts(HWND hWnd) {
	vector<string> comPorts;

	HKEY hKey = NULL;
	// �򿪴���ӳ���
    if (RegOpenKeyEx( HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return comPorts;
    }

	DWORD index = 0;
    CHAR valueName[256];
    CHAR data[256];
    DWORD valueNameSize, dataSize, type;
    // ö������ֵ
    while (true)
    {
        valueNameSize = sizeof(valueName);
        dataSize      = sizeof(data);
        LONG ret = RegEnumValue( hKey, index++, valueName, &valueNameSize, NULL, &type, reinterpret_cast<LPBYTE>(data), &dataSize);
        if (ret == ERROR_NO_MORE_ITEMS)
            break;
        if (ret == ERROR_SUCCESS && type == REG_SZ)
        {
            // data ���� "COMx"
            string portName(data);
            comPorts.push_back("\\\\.\\" + portName);
        }
    }

    RegCloseKey(hKey);
    return comPorts;
}

bool Once()
{
    // ʹ�õ�ǰ��ִ���ļ�����·��������һ��Ψһ�Ĺ����ڴ�����
    TCHAR exePath[MAX_PATH] = {0};
    GetModuleFileName(nullptr, exePath, MAX_PATH);

    // DJB2/FNV-1a ��Ϲ�ϣ���򵥡�����������ƽ̨һ��
    DWORD hash = 2166136261u;
    for (TCHAR *p = exePath; *p; ++p)
        hash = (hash ^ (BYTE)*p) * 16777619u;

    // ƴ�ӳ�ȫ�������ռ䣨�� Session�����ƣ�ȷ������ / Զ��������ͬ����Ч
    TCHAR mapName[64];
    sprintf_s(mapName, sizeof(mapName), "Local\\iGameLCD_%08X", hash);

    // ��������򿪣�һ��ҳ�ļ��󱸵��ļ�ӳ����󣬴�С�ɷǳ�С
    HANDLE g_hMap = CreateFileMapping(INVALID_HANDLE_VALUE,  // ʹ��ϵͳҳ�ļ�
                               nullptr,              // Ĭ�ϰ�ȫ����
                               PAGE_READWRITE,       // ��д����
                               0,                    // �� 32 λ��С��0 ��ʾ 32 λ��
                               sizeof(DWORD),        // 1 ҳ����
                               mapName);             // Ψһ����

    if (!g_hMap)
    {
        // �����������ʱ������ false
		printf("CreateFileMapping err=%d\n", GetLastError());
        return false;
    }

    // ��������Ѵ��ڣ�˵��������ʵ��������
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(g_hMap);
        g_hMap = nullptr;
        return false;
    }

    // ��ʵ�����������ٹر� g_hMap����ά��ռ��
    return true;
}

LRESULT CALLBACK WndProc( HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam )
{
	vector<string> comPorts;
	char *comPort=nullptr;
	switch( nMsg )
	{
	case WM_DEVICECHANGE:
		comPort = getNewCOMName(hWnd, wParam, lParam);
		if (comPort!=nullptr) {
			work->TryWorking(comPort);
			free(comPort);
		}
		break;
	case WM_HOTKEY:
		HotKey( hWnd, nMsg, wParam, lParam );
		return 0;
	case WM_CREATE:
		if (!Once()) {
			PostMessage(hWnd, WM_DESTROY, 0, 0);
			return 0;
		}
		RegisterHotKey(hWnd,30, MOD_CONTROL|MOD_ALT, 'L');
		work = new Work;
		// 1) ���Ĵ������豸�ӿ� (GUID_DEVINTERFACE_COMPORT)
		g_hCOMNotify = registerListenCOM(hWnd);
		// 2) ��ʼö������ COM ��
		comPorts = enumerateExistingComPorts(hWnd);
		if (comPorts.size()==0) {
			MessageBox( hWnd, "�����½���LCD�豸!", "Fail", MB_OK|MB_TOPMOST );
		} else
		for (size_t i = 0; i < comPorts.size(); ++i) {
			printf("TryWorking: %s\n", comPorts[i].c_str());
			work->TryWorking(comPorts[i].c_str());
		}
		return 0;
	case WM_DESTROY:
		if (g_hCOMNotify) {
			UnregisterDeviceNotification(g_hCOMNotify);
			g_hCOMNotify = nullptr;
			UnregisterHotKey(hWnd, 30);
		}
		if (work!=nullptr) {
			delete(work);
			work = nullptr;
		}
		PostQuitMessage(0);
		break;
	case WM_QUERYENDSESSION:
		PostMessage(hWnd, WM_DESTROY, 0, 0);
		break;
	case WM_ENDSESSION:
        if (wParam) DestroyWindow(hWnd); // TRUE = �Ự���ڽ���
        break;
	}
	return DefWindowProc( hWnd, nMsg, wParam, lParam );
}

BOOL RegisterWnd( LPSTR pszClassName )
{
	WNDCLASSEX wce = { 0 };
	wce.cbSize = sizeof( wce );
	wce.cbWndExtra = 0;
	wce.cbClsExtra = 0;
	wce.hCursor = NULL;
	wce.hIcon = NULL;
	wce.hIconSm = NULL;
	wce.hInstance = g_hInst;
	wce.lpfnWndProc = WndProc;
	wce.lpszClassName = pszClassName;
	wce.lpszMenuName = NULL;

	return RegisterClassEx( &wce );
}

HWND CreateWnd( LPSTR pszClassName )
{
	HWND hWnd = CreateWindowEx( 0,
		pszClassName, WND_NAME, NULL,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, g_hInst, NULL);
	return hWnd;
}

void Message( HWND hWnd )
{
	MSG msg = {0};
	while( GetMessage( &msg, NULL, 0, 0 ) )
	{
		DispatchMessage( &msg );
	}
}

int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	g_hInst = hInstance;
	RegisterWnd( WND_NAME );
	hWnd = CreateWnd( WND_NAME );
	Message( hWnd );
	return 0;
}
