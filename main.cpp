// lcd.cpp : 定义应用程序的入口点。
//

#include<Windows.h>
#include<stdio.h>
#include <dbt.h>        // WM_DEVICECHANGE / DEV_BROADCAST_*
#include <vector>
#include <string>
#include "work.h"

#ifdef _DEBUG
// Debug: 控制台子系统
#pragma comment(linker, "/SUBSYSTEM:CONSOLE /ENTRY:WinMainCRTStartup")
#else
// Release: 正常的 Windows GUI 子系统
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
			MessageBox( hWnd, "进程已退出!", "Succ", MB_OK|MB_TOPMOST );
			PostMessage(hWnd, WM_DESTROY, 0, 0);
		}
	}
}

HDEVNOTIFY registerListenCOM( HWND hWnd ) {
	// 1) 只订阅 GUID_DEVINTERFACE_COMPORT 类型的设备通知
	DEV_BROADCAST_DEVICEINTERFACE di={0};
	di.dbcc_size       = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	di.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	di.dbcc_classguid  = GUID_DEVINTERFACE_COMPORT;

	HDEVNOTIFY notify = RegisterDeviceNotification( hWnd, &di, DEVICE_NOTIFY_WINDOW_HANDLE );
	if ( !notify ) {
		MessageBox( hWnd, "无法侦听新COM事件!", "Fail", MB_OK|MB_TOPMOST );
	}
	return notify;
}

// 通过设备路径直接获取 COM 名称
LPSTR parseComName(LPCSTR devicePath)
{
	if (!devicePath || !*devicePath) return nullptr;
	char *path = _strdup(devicePath);

	// 1. 去掉前缀 "\\?\"
    char kPrefix[] = "\\\\?\\";
    if (strncmp(path, kPrefix, strlen(kPrefix)) == 0)
    {
        memmove(path, path + strlen(kPrefix), strlen(path) - strlen(kPrefix) + 1);
    }

    // 2. 去掉最后的 "#{GUID}"
    if (char* brace = strstr(path, "#{"))
        *brace = '\0';

    // 3. 把所有 '#' 替换成 '\\'
    for (char* c = path; *c; c++)
        if (*c == '#') *c = '\\';

    // 4. 拼接注册表完整路径
	string regPath = "SYSTEM\\CurrentControlSet\\Enum\\" + string(path) + "\\Device Parameters";
	free(path);

    // 5. 打开键并读取 PortName
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

			if (wParam == DBT_DEVICEARRIVAL) { // 插入事件
				LPSTR comName = parseComName(pDev->dbcc_name);
				if (comName!=nullptr)
					MessageBox(hWnd, "检测到新LCD接入", "Succ", MB_OK | MB_ICONINFORMATION);
				else
					MessageBox(hWnd, "无法获取COM名", "Fail", MB_OK | MB_ICONINFORMATION);
				return comName;
			}
		}
	}
	return nullptr;
}

vector<string> enumerateExistingComPorts(HWND hWnd) {
	vector<string> comPorts;

	HKEY hKey = NULL;
	// 打开串口映射键
    if (RegOpenKeyEx( HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return comPorts;
    }

	DWORD index = 0;
    CHAR valueName[256];
    CHAR data[256];
    DWORD valueNameSize, dataSize, type;
    // 枚举所有值
    while (true)
    {
        valueNameSize = sizeof(valueName);
        dataSize      = sizeof(data);
        LONG ret = RegEnumValue( hKey, index++, valueName, &valueNameSize, NULL, &type, reinterpret_cast<LPBYTE>(data), &dataSize);
        if (ret == ERROR_NO_MORE_ITEMS)
            break;
        if (ret == ERROR_SUCCESS && type == REG_SZ)
        {
            // data 中是 "COMx"
            string portName(data);
            comPorts.push_back("\\\\.\\" + portName);
        }
    }

    RegCloseKey(hKey);
    return comPorts;
}

bool Once()
{
    // 使用当前可执行文件完整路径派生出一个唯一的共享内存名称
    TCHAR exePath[MAX_PATH] = {0};
    GetModuleFileName(nullptr, exePath, MAX_PATH);

    // DJB2/FNV-1a 混合哈希：简单、无依赖、跨平台一致
    DWORD hash = 2166136261u;
    for (TCHAR *p = exePath; *p; ++p)
        hash = (hash ^ (BYTE)*p) * 16777619u;

    // 拼接出全局命名空间（跨 Session）名称，确保服务 / 远程桌面下同样生效
    TCHAR mapName[64];
    sprintf_s(mapName, sizeof(mapName), "Local\\iGameLCD_%08X", hash);

    // 创建（或打开）一个页文件后备的文件映射对象，大小可非常小
    HANDLE g_hMap = CreateFileMapping(INVALID_HANDLE_VALUE,  // 使用系统页文件
                               nullptr,              // 默认安全属性
                               PAGE_READWRITE,       // 读写即可
                               0,                    // 高 32 位大小（0 表示 32 位）
                               sizeof(DWORD),        // 1 页足矣
                               mapName);             // 唯一名称

    if (!g_hMap)
    {
        // 出现意外错误时，返回 false
		printf("CreateFileMapping err=%d\n", GetLastError());
        return false;
    }

    // 如果对象已存在，说明有其它实例在运行
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(g_hMap);
        g_hMap = nullptr;
        return false;
    }

    // 首实例，后续不再关闭 g_hMap，以维持占用
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
		// 1) 订阅串口类设备接口 (GUID_DEVINTERFACE_COMPORT)
		g_hCOMNotify = registerListenCOM(hWnd);
		// 2) 初始枚举已有 COM 口
		comPorts = enumerateExistingComPorts(hWnd);
		if (comPorts.size()==0) {
			MessageBox( hWnd, "请重新接入LCD设备!", "Fail", MB_OK|MB_TOPMOST );
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
        if (wParam) DestroyWindow(hWnd); // TRUE = 会话正在结束
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
