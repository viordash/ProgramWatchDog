/*
Контроль за работой (зависанием) программ с оконным интерфейсом (используется функция SendMessageTimeout).

При запуске ProgramWatchDog, если контролируемая программа еще не запущена,
то ProgramWatchDog запускает ее.

Если контролируемая программа перестала отвечать, то ProgramWatchDog попытается ее закрыть.
При удачном закрытии программы, то ProgramWatchDog заново ее запускает.
Если закрыть не удалось, то ProgramWatchDog перезагружает компьютер.

Вывод лог-сообщений в консоль, при закрытии ProgramWatchDog, создается лог-файл,
в папке, где находится контролируемая программа, в подпапке "..\LOG",

Использование:
Создаете bat-файл или ярлык, с командой:

Полный путь к программе ProgramWatchDog "Полный путь к контролируемой программе"

пример
C:\PROJECTS\TEST\ProgramWatchDog.exe "C:\PROJECTS\TEST\TestAppl.exe"

Если контролируемая программа должна запускаться и после перезагрузки компьютера,
то вставьте bat файл или ярлык ProgramWatchDog-а в "Автозагрузка" ОС,
и он сам запустит контролируемую программу
*/

#include <stdio.h>
#include <tchar.h>
#include <WinDef.h>
#include <Windows.h>
#include <stdio.h>

#include <psapi.h>
#pragma comment( lib, "psapi.lib" )

#pragma warning(disable: 28159)
#pragma warning(disable: 26481)
#pragma warning(disable: 26493)
#pragma warning(disable: 26408)

#define RESPOND_TIMEOUT (7000 - 4000)

typedef struct _TWindowFind {
	BOOL IsFound;
	DWORD ProcessID;
} TWindowFind, * PTWindowFind;

HANDLE MainWindow;
_TCHAR arg_0[MAX_PATH];

static void PrintLog(const _TCHAR* AText, _TCHAR* AParamStr, BOOL AWithDelimeter) noexcept {
	_TCHAR Buffer[MAX_PATH];
	SYSTEMTIME SystemTime;
	GetLocalTime(&SystemTime);
	if(AWithDelimeter) {
		swprintf(&Buffer[0], MAX_PATH, L"--------%02d.%02d.%04d---------\r\n", SystemTime.wDay, SystemTime.wMonth, SystemTime.wYear);
		wprintf(&Buffer[0], AParamStr);
	}
	swprintf(&Buffer[0], MAX_PATH, L"%02d:%02d:%02d.%03d \t %s", SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond, SystemTime.wMilliseconds, AText);
	wprintf(&Buffer[0], AParamStr);
}

static BOOL AppIsResponding(void) noexcept {
	DWORD_PTR Res;
	HWND handle;

	handle = (HWND)MainWindow;
	if((handle != 0) && (handle != INVALID_HANDLE_VALUE)) {
		return (SendMessageTimeout(handle, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, RESPOND_TIMEOUT, &Res) != 0);
	} else {
		return(FALSE);
	}
}


static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) noexcept {
	if(IsWindow(hWnd) && !((PTWindowFind)lParam)->IsFound) {
		DWORD pid;
		const DWORD dwTheardId = GetWindowThreadProcessId(hWnd, &pid);
		if(pid == ((PTWindowFind)lParam)->ProcessID) {
			MainWindow = hWnd;
			((PTWindowFind)lParam)->IsFound = TRUE;
		}
	}
	return TRUE;
}


static DWORD procs[0x1000]; //массив для хранения дескрипторов процессов
static BOOL AppIsExist(const _TCHAR* lpExeName, BOOL IsTerminate) noexcept {
	HANDLE ph;
	INT i;
	DWORD count, cm; //количество процессов
	HMODULE mh; //дескриптор модуля
	_TCHAR ModName[MAX_PATH]; //имя модуля
	TWindowFind WindowFind;

	if(EnumProcesses(&procs[0], sizeof(procs), &count) != 0) {
		for(i = 0; i < count / sizeof(DWORD); ++i) {
			WindowFind.IsFound = FALSE;
			MainWindow = INVALID_HANDLE_VALUE;
			ph = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, FALSE, procs[i]);
			if(ph == NULL) {
				continue;
			}
			if(!EnumProcessModules(ph, &mh, sizeof(mh), &cm)) {
				const DWORD dw = GetLastError();
				CloseHandle(ph);
			}
			if(GetModuleFileNameEx(ph, mh, &ModName[0], sizeof(ModName) / sizeof(ModName[0])) <= 0) {
				const DWORD dw = GetLastError();
				CloseHandle(ph);
				continue;
			}

			if(wcscmp(lpExeName, &ModName[0]) == 0) {
				if(!IsTerminate) {
					WindowFind.IsFound = FALSE;
					WindowFind.ProcessID = GetProcessId(ph);
					EnumWindows(EnumWindowsProc, (LPARAM)&WindowFind);
				} else {
					WindowFind.IsFound = TerminateProcess(ph, 1);
				}
			}
			CloseHandle(ph);
			if(WindowFind.IsFound) {
				return(TRUE);
			}
		}
	}
	return (FALSE);
}

static BOOL RunProgram(_TCHAR* lpExeName, _TCHAR* lpCommandLine) noexcept {
	_TCHAR ExeCommandLine[MAX_PATH];
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	wsprintf(&ExeCommandLine[0], L"%s%s", lpExeName, lpCommandLine);
	const BOOL res = CreateProcess(NULL, &ExeCommandLine[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);
	if(res) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	return res;
}

static BOOL SetPrivilege(BOOL enable) noexcept {
	BOOL res;
	TOKEN_PRIVILEGES tpPrev, tkp;
	HANDLE hToken;
	DWORD dwRetLen;

	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return(FALSE);
	tkp.PrivilegeCount = 1;
	if(!LookupPrivilegeValue(NULL, L"SeShutdownPrivilege", &(tkp.Privileges[0].Luid))) return(FALSE);
	if(enable)
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	else
		tkp.Privileges[0].Attributes = 0;
	dwRetLen = 0;
	res = AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tpPrev), &tpPrev, &dwRetLen);
	CloseHandle(hToken);
	return(res);
}

static BOOL RebootOS(_TCHAR* lpExeName) noexcept {
	if(SetPrivilege(TRUE)) {
		_TCHAR msg[MAX_PATH];
		wsprintf(&msg[0], L"System reboot due to program '%s' freezing", lpExeName);
		InitiateSystemShutdownEx(NULL, &msg[0], 10, TRUE, TRUE, SHTDN_REASON_MAJOR_SOFTWARE);
		SetPrivilege(FALSE);
		return(TRUE);
	} else return(FALSE);
}


static void WriteBufToFile(FILE* f, PCHAR_INFO buf, COORD bsize, SMALL_RECT* size) noexcept {
	int X, Y;
	for(Y = 0; Y < bsize.Y; Y++) {
		for(X = 0; X < bsize.X; X++)
			fputc(buf[X + (Y * bsize.X)].Char.AsciiChar, f);
		fputc('\n', f);
	}
}


BOOL ConsoleToFile() {
	_TCHAR LogFileName[MAX_PATH];
	_TCHAR* pBuffer;
	SYSTEMTIME SystemTime;
	STARTUPINFO spt;
	PROCESS_INFORMATION pi;
	HANDLE hRead;
	PCHAR_INFO buf;
	COORD  bcoord, bpos;
	CONSOLE_SCREEN_BUFFER_INFO cinfo;
	SMALL_RECT wnd;
	FILE* f;
	BOOL res = FALSE;
	hRead = GetStdHandle(STD_OUTPUT_HANDLE);

	GetConsoleScreenBufferInfo(hRead, &cinfo);
	bpos.X = 0;
	bpos.Y = 0;
	bcoord.X = cinfo.srWindow.Right - cinfo.srWindow.Left + 1;
	bcoord.Y = cinfo.srWindow.Bottom - cinfo.srWindow.Top + 1;

	if(buf = (PCHAR_INFO)malloc(bcoord.X * bcoord.Y * sizeof(CHAR_INFO))) {
		wcscpy(&LogFileName[0], &arg_0[0]);
		pBuffer = wcsrchr(&LogFileName[0], '\\');
		pBuffer++;
		//	pBuffer = 0;	
		wcscpy(pBuffer, L"Log\\");
		pBuffer += wcslen(L"Log\\");
		if(_wmkdir(&LogFileName[0]) != 0) {
		}
		GetLocalTime(&SystemTime);
		swprintf(pBuffer, 20, L"WDG_%04d%02d%02d_%02d%02d%02d", SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay, SystemTime.wHour,
			SystemTime.wMinute, SystemTime.wMinute);
		wcscat(&LogFileName[0], L".log");
		if(f = _wfopen(&LogFileName[0], L"wt")) {
			wnd = cinfo.srWindow;
			if(ReadConsoleOutput(hRead, buf, bcoord, bpos, &wnd)) {
				WriteBufToFile(f, buf, bcoord, &wnd);
				res = TRUE;
			}
			fclose(f);
		}
		free(buf);
	}
	return (res);
}


BOOL CtrlHandler(DWORD fdwCtrlType) {
	switch(fdwCtrlType) {
		// CTRL-CLOSE: confirm that the user wants to exit. 
		case CTRL_BREAK_EVENT:
		case CTRL_C_EVENT:
			return(TRUE);
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_CLOSE_EVENT:
			wprintf(L"\r\n");
			PrintLog(L"Closing...\r\n", NULL, TRUE);
			ConsoleToFile();
			return(FALSE);
		default:
			return FALSE;
	}
}


int _tmain(int argc, _TCHAR* argv[]) {
	_TCHAR ExeFullName[MAX_PATH];
	_TCHAR ExeCommandLine[MAX_PATH];
	_TCHAR ExeName[MAX_PATH];
	_TCHAR* pBuffer;
	INT i, NotRespondCounter, TerminateCount;

	wprintf(L"Program work watching. (c)viordash 2012. viordash@mail.ru\r\n");
	wprintf(L"\r\n");
	Sleep(20);

	if((argc < 2) || ((pBuffer = wcsrchr(argv[1], '\\')) == NULL)) {
		wcscpy(&ExeFullName[0], argv[0]);
		pBuffer = wcsrchr(&ExeFullName[0], '\\');
		pBuffer++;
		wprintf(L"Use:  %s <full path of watching program>\r\n", pBuffer);
		wprintf(L"\r\n");
		wprintf(L"Press any key to close.\r\n");
		Sleep(20);
		while(!_getwch()) Sleep(100);
		return 0;
	} else wcscpy(&ExeFullName[0], argv[1]);
	ExeCommandLine[0] = 0;
	for(i = 2; i < argc; i++) {
		wsprintf(&ExeCommandLine[0], L"%s %s", &ExeCommandLine[0], argv[i]);
	}

	wcscpy(&ExeName[0], pBuffer + 1);
	wcscpy(&arg_0[0], argv[0]);
	wprintf(&arg_0[0]);
	wprintf(L"\r\n");
	wprintf(L"\r\n");

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
	PrintLog(L"Watching start [%s]...\r\n", &ExeName[0], TRUE);
	Sleep(15000);
	MainWindow = INVALID_HANDLE_VALUE;
	TerminateCount = 0;
	while(TRUE) {
		Sleep(100);
		if(!AppIsExist(&ExeFullName[0], FALSE)) {
			PrintLog(L"Not run! Try execute...\r\n", NULL, FALSE);
			RunProgram(&ExeFullName[0], &ExeCommandLine[0]);
			Sleep(10000);
			continue;
		} else PrintLog(L"Is run. Check respond...\r\n", NULL, FALSE);

		NotRespondCounter = 0;
		while(NotRespondCounter++ < 20 + 20) {
			if(AppIsResponding()) {
				if(NotRespondCounter > 1) wprintf(L"\r\n");
				NotRespondCounter = 0;
				TerminateCount = 0;
				Sleep(1000);
				_putwch(L'\\');
				Sleep(1000);
				_putwch(L'\b');
				_putwch(L'|');
				Sleep(1000);
				_putwch(L'\b');
				_putwch(L'/');
				Sleep(1000);
				_putwch(L'\b');
				_putwch(L'-');
				Sleep(1000);
				_putwch(L'\b');
			} else {
				if(NotRespondCounter < 2) PrintLog(L"No response. Waiting ", NULL, FALSE);
				Sleep(10);
				if(!Beep(3000, 350)) Sleep(350);
				if(!Beep(1500, 350)) Sleep(350);
				_putwch(L'*');
				if(NotRespondCounter % 5 == 0) {
					if(!AppIsExist(&ExeFullName[0], FALSE)) {
						NotRespondCounter += 3;
					}
				}
			}
		}
		wprintf(L"\r\n");
		PrintLog(L"No response. Terminate...\r\n", NULL, FALSE);
		if(!AppIsExist(&ExeFullName[0], TRUE)) {
			PrintLog(L"Terminate error...\r\n", NULL, FALSE);
		}
		ConsoleToFile();
		Sleep(5000);
		if(++TerminateCount >= 5) {
			if(!RebootOS(&ExeFullName[0])) {
				PrintLog(L"Reboot error...\r\n", NULL, FALSE);
			}
		}
	}

	return 0;
}

