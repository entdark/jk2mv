#include <winsock2.h>
#include <windows.h>
#include <float.h>
#include <io.h>
#include <shlobj.h>
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

char *Sys_GetCurrentUser( void )
{
	static char s_userName[1024];
	DWORD size = sizeof( s_userName );


	if ( !GetUserNameA( s_userName, &size ) )
		strcpy( s_userName, "player" );

	if ( !s_userName[0] )
	{
		strcpy( s_userName, "player" );
	}

	return s_userName;
}

char *Sys_DefaultHomePath(void) {
#if !defined(PORTABLE)
	char szPath[MAX_PATH];
	static char homePath[MAX_OSPATH];

	if (!SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, szPath))) {
		Com_Printf("Unable to detect CSIDL_PERSONAL\n");
		return NULL;
	}

	Com_sprintf(homePath, sizeof(homePath), "%s%cjk2mv", szPath, PATH_SEP);
	return homePath;
#else
	return Sys_Cwd();
#endif
}

// read the path from the registry on windows... steam also sets it, but with "InstallPath" instead of "Install Path"
char *Sys_DefaultAssetsPath() {
#ifdef INSTALLED
	HKEY hKey;
	static char installPath[MAX_OSPATH];
	DWORD installPathSize;

	// force 32bit registry
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\LucasArts Entertainment Company LLC\\Star Wars JK II Jedi Outcast\\1.0",
		0, KEY_WOW64_32KEY|KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
		return NULL;
	}

	installPathSize = sizeof(installPath);
	if (RegQueryValueExA(hKey, "Install Path", NULL, NULL, (LPBYTE)installPath, &installPathSize) != ERROR_SUCCESS) {
		if (RegQueryValueExA(hKey, "InstallPath", NULL, NULL, (LPBYTE)installPath, &installPathSize) != ERROR_SUCCESS) {
			RegCloseKey(hKey);
			return NULL;
		}
	}

	RegCloseKey(hKey);
	Q_strcat(installPath, sizeof(installPath), "\\GameData");
	return installPath;
#else
	return Sys_Cwd();
#endif
}

char *Sys_DefaultInstallPath(void)
{
	return Sys_Cwd();
}

void Sys_Sleep(int msec) {
	if (msec == 0)
		return;

#ifdef DEDICATED
	if (msec < 0)
		WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), INFINITE);
	else
		WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), msec);
#else
	// Client Sys_Sleep doesn't support waiting on stdin
	if (msec < 0)
		return;

	Sleep(msec);
#endif
}

/*
==============================================================

DIRECTORY SCANNING

==============================================================
*/

#define	MAX_FOUND_FILES	0x1000

void Sys_ListFilteredFiles(const char *basedir, char *subdirs, char *filter, char **psList, int *numfiles) {
	char		search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
	char		filename[MAX_OSPATH];
	HANDLE		findhandle;
	WIN32_FIND_DATAA findinfo;

	if (*numfiles >= MAX_FOUND_FILES - 1) {
		return;
	}

	if (strlen(subdirs)) {
		Com_sprintf(search, sizeof(search), "%s\\%s\\*", basedir, subdirs);
	} else {
		Com_sprintf(search, sizeof(search), "%s\\*", basedir);
	}

	findhandle = FindFirstFileA(search, &findinfo);
	if (findhandle == INVALID_HANDLE_VALUE) {
		return;
	}

	do {
		if (findinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (Q_stricmp(findinfo.cFileName, ".") && Q_stricmp(findinfo.cFileName, "..")) {
				if (strlen(subdirs)) {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, findinfo.cFileName);
				} else {
					Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", findinfo.cFileName);
				}
				Sys_ListFilteredFiles(basedir, newsubdirs, filter, psList, numfiles);
			}
		}
		if (*numfiles >= MAX_FOUND_FILES - 1) {
			break;
		}
		Com_sprintf(filename, sizeof(filename), "%s\\%s", subdirs, findinfo.cFileName);
		if (!Com_FilterPath(filter, filename, qfalse))
			continue;
		psList[*numfiles] = CopyString(filename);
		(*numfiles)++;
	} while (FindNextFileA(findhandle, &findinfo) != 0);

	FindClose(findhandle);
}

static qboolean strgtr(const char *s0, const char *s1) {
	int l0, l1, i;

	l0 = (int)strlen(s0);
	l1 = (int)strlen(s1);

	if (l1<l0) {
		l0 = l1;
	}

	for (i = 0; i<l0; i++) {
		if (s1[i] > s0[i]) {
			return qtrue;
		}
		if (s1[i] < s0[i]) {
			return qfalse;
		}
	}
	return qfalse;
}

char **Sys_ListFiles(const char *directory, const char *extension, char *filter, int *numfiles, qboolean wantsubs) {
	char		search[MAX_OSPATH];
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	HANDLE		findhandle;
	WIN32_FIND_DATAA findinfo;
	int			flag;
	int			i;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);

		list[nfiles] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy), TAG_LISTFILES);
		for (i = 0; i < nfiles; i++) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if (!extension) {
		extension = "";
	}

	// passing a slash as extension will find directories
	if (extension[0] == '/' && extension[1] == 0) {
		extension = "";
		flag = 0;
	} else {
		flag = FILE_ATTRIBUTE_DIRECTORY;
	}

	Com_sprintf(search, sizeof(search), "%s\\*%s", directory, extension);

	// search
	nfiles = 0;

	findhandle = FindFirstFileA(search, &findinfo);
	if (findhandle == INVALID_HANDLE_VALUE) {
		*numfiles = 0;
		return NULL;
	}

	do {
		if ((!wantsubs && flag ^ (findinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) || (wantsubs && findinfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			if (nfiles == MAX_FOUND_FILES - 1) {
				break;
			}
			list[nfiles] = CopyString(findinfo.cFileName);
			nfiles++;
		}
	} while (FindNextFileA(findhandle, &findinfo) != 0);

	list[nfiles] = 0;

	FindClose(findhandle);

	// return a copy of the list
	*numfiles = nfiles;

	if (!nfiles) {
		return NULL;
	}

	listCopy = (char **)Z_Malloc((nfiles + 1) * sizeof(*listCopy), TAG_LISTFILES);
	for (i = 0; i < nfiles; i++) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	do {
		flag = 0;
		for (i = 1; i<nfiles; i++) {
			if (strgtr(listCopy[i - 1], listCopy[i])) {
				char *temp = listCopy[i];
				listCopy[i] = listCopy[i - 1];
				listCopy[i - 1] = temp;
				flag = 1;
			}
		}
	} while (flag);

	return listCopy;
}

void	Sys_FreeFileList(char **psList) {
	int		i;

	if (!psList) {
		return;
	}

	for (i = 0; psList[i]; i++) {
		Z_Free(psList[i]);
	}

	Z_Free(psList);
}

/*
==============
Sys_Cwd
==============
*/
char *Sys_Cwd(void) {
	static char cwd[MAX_OSPATH];
	GetCurrentDirectoryA(sizeof(cwd), cwd);
	return cwd;
}

/*
=================
Sys_UnloadModuleLibrary
=================
*/
void Sys_UnloadModuleLibrary(void *dllHandle) {
	if (!dllHandle) {
		return;
	}

	if (!FreeLibrary((HMODULE)dllHandle)) {
		Com_Error(ERR_FATAL, "Sys_UnloadDll FreeLibrary failed");
	}
}

/*
=================
Sys_LoadModuleLibrary

Used to load a module (jk2mpgame, cgame, ui) dll
=================
*/
void *Sys_LoadModuleLibrary(const char *name, qboolean mvOverride, intptr_t(QDECL **entryPoint)(int, ...), intptr_t(QDECL *systemcalls)(intptr_t, ...)) {
	HMODULE	libHandle;
	void	(QDECL *dllEntry)(intptr_t(QDECL *syscallptr)(intptr_t, ...));
	char	*path, *filePath;
	char	filename[MAX_QPATH];

	Com_sprintf(filename, sizeof(filename), "%s_" ARCH_STRING "." LIBRARY_EXTENSION, name);

	if (!mvOverride) {
		path = Cvar_VariableString("fs_basepath");
		filePath = FS_BuildOSPath(path, NULL, filename);

		Com_DPrintf("Loading module: %s...", filePath);
		libHandle = LoadLibraryA(filePath);
		if (!libHandle) {
			Com_DPrintf(" failed!\n");
			path = Cvar_VariableString("fs_homepath");
			filePath = FS_BuildOSPath(path, NULL, filename);

			Com_DPrintf("Loading module: %s...", filePath);
			libHandle = LoadLibraryA(filePath);
			if (!libHandle) {
				Com_DPrintf(" failed!\n");
				return NULL;
			} else {
				Com_DPrintf(" success!\n");
			}
		} else {
			Com_DPrintf(" success!\n");
		}
	} else {
		char dllPath[MAX_PATH];
		path = Cvar_VariableString("fs_basepath");
		Com_sprintf(dllPath, sizeof(dllPath), "%s\\%s", path, filename);

		Com_DPrintf("Loading module: %s...", dllPath);
		libHandle = LoadLibraryA(dllPath);
		if (!libHandle) {
			Com_DPrintf(" failed!\n");
			return NULL;
		} else {
			Com_DPrintf(" success!\n");
		}
	}

	dllEntry = (void (QDECL *)(intptr_t(QDECL *)(intptr_t, ...)))GetProcAddress(libHandle, "dllEntry");
	*entryPoint = (intptr_t(QDECL *)(int, ...))GetProcAddress(libHandle, "vmMain");
	if (!*entryPoint) {
		Com_DPrintf("Could not find vmMain in %s\n", filename);
		FreeLibrary(libHandle);
		return NULL;
	}

	if (!dllEntry) {
		Com_DPrintf("Could not find dllEntry in %s\n", filename);
		FreeLibrary(libHandle);
		return NULL;
	}

	dllEntry(systemcalls);

	return libHandle;
}

/*
==============
Sys_Mkdir
==============
*/
qboolean Sys_Mkdir(const char *path) {
	if (!CreateDirectoryA(path, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS)
			return qfalse;
	}
	return qtrue;
}

/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds(bool baseTime) {
	static int sys_timeBase = timeGetTime();
	int			sys_curtime;

	sys_curtime = timeGetTime();
	if (!baseTime) {
		sys_curtime -= sys_timeBase;
	}

	return sys_curtime;
}

int Sys_Milliseconds2(void) {
	return Sys_Milliseconds(false);
}

static UINT timerResolution = 0;

void Sys_PlatformInit(void) {
	TIMECAPS ptc;
	if (timeGetDevCaps(&ptc, sizeof(ptc)) == MMSYSERR_NOERROR)
	{
		timerResolution = ptc.wPeriodMin;

		if (timerResolution > 1)
		{
			Com_Printf("Warning: Minimum supported timer resolution is %ums "
				"on this system, recommended resolution 1ms\n", timerResolution);
		}

		timeBeginPeriod(timerResolution);
	} else
		timerResolution = 0;
}

void Sys_PlatformExit(void)
{
	if (timerResolution)
		timeEndPeriod(timerResolution);
}


// from ioq3 requires sse
// i do not care about processors without sse
#ifdef __GNUC__
#if idx64
  #define EAX "%%rax"
  #define EBX "%%rbx"
  #define ESP "%%rsp"
  #define EDI "%%rdi"
#else
  #define EAX "%%eax"
  #define EBX "%%ebx"
  #define ESP "%%esp"
  #define EDI "%%edi"
#endif

long Q_ftol(float f) {
    long retval;

    __asm__ volatile (
        "cvttss2si %1, %0\n"
        : "=r" (retval)
        : "x" (f)
    );

    return retval;
}

int Q_VMftol() {
    int retval;

    __asm__ volatile (
        "movss (" EDI ", " EBX ", 4), %%xmm0\n"
        "cvttss2si %%xmm0, %0\n"
        : "=r" (retval)
        :
        : "%xmm0"
    );

    return retval;
}

static unsigned char ssemask[16] __attribute__((aligned(16))) = {
	"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00"
};

void Sys_SnapVector(vec3_t vec) {
	__asm__ volatile
	(
		"movaps (%0), %%xmm1\n"
		"movups (%1), %%xmm0\n"
		"movaps %%xmm0, %%xmm2\n"
		"andps %%xmm1, %%xmm0\n"
		"andnps %%xmm2, %%xmm1\n"
		"cvtps2dq %%xmm0, %%xmm0\n"
		"cvtdq2ps %%xmm0, %%xmm0\n"
		"orps %%xmm1, %%xmm0\n"
		"movups %%xmm0, (%1)\n"
		:
		: "r" (ssemask), "r" (vec)
		: "memory", "%xmm0", "%xmm1", "%xmm2"
	);

}
#endif

#include <string>
using namespace std;

#define PROGRAM_MUTEX L"jk2mvmp"
HANDLE mutex;

typedef struct {
	wstring extension;
	wstring desc;
} extensionsTable_t;

static extensionsTable_t et[] = {
	//should we reg it? q3mme and other mme mods use it too
//	{".mme", "MovieMaker's Edition Demo"},
	{L".dm_15", L"Jedi Outcast Demo (v1.02/1.03)"},
	{L".dm_16", L"Jedi Outcast Demo (v1.04)"},
};

TCHAR *GetStringRegKey(HKEY hkey, const TCHAR *valueName) {
	if (!hkey) {
		return NULL;
	}
    static WCHAR szBuffer[512];
    DWORD dwBufferSize = sizeof(szBuffer);
    LSTATUS nError = RegQueryValueEx(hkey, valueName, 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
    if (ERROR_SUCCESS == nError) {
        return szBuffer;
    }
    return NULL;
}

bool AddRegistry(const HKEY key, const TCHAR *subkey, const TCHAR *value, const TCHAR *valueName = NULL) {
	Com_DPrintf(S_COLOR_YELLOW"AddRegistry(%u,%s,%s,%s)\n", key, subkey, value, valueName ? valueName : L"NULL");
	HKEY hkey;
	LSTATUS nError = RegOpenKeyEx(key, subkey, 0, KEY_READ, &hkey);
	if (nError != ERROR_SUCCESS && nError != ERROR_FILE_NOT_FOUND) {
		Com_DPrintf(S_COLOR_RED"RegOpenKeyEx(%u,%s,%s) error: %d\n", key, subkey, value, nError);
		return false;
	}
	const TCHAR *setValue = GetStringRegKey(hkey, valueName);
	RegCloseKey(hkey);
	//ignore the same value
	if (!setValue || wcsicmp(setValue, value)) {
		nError = RegCreateKeyEx(key, subkey, 0, 0, 0, KEY_ALL_ACCESS, 0, &hkey, 0);
		if (nError == ERROR_SUCCESS) {
			RegSetValueEx(hkey, valueName, 0, REG_SZ, (BYTE*)value, (wcslen(value)+1)*sizeof(TCHAR));
			RegCloseKey(hkey);
			return true;
		} else {
			Com_DPrintf(S_COLOR_RED"RegCreateKeyEx(%u,%s,%s) error: %d\n", key, subkey, value, nError);
		}
	}
	return false;
}

void Sys_RegisterFileTypes(TCHAR *program) {
	wstring action=L"jk2mvmp";
	bool refresh = false; //once true - forever true
	for (int i = 0; i < ARRAY_LEN(et); i++) {
		wstring app = program + wstring(L" +set fs_game \"mme\" +demo \"%1\" del");
		wstring extension = L"Software\\Classes\\" + et[i].extension;
		wstring desc = et[i].desc;
		wstring icon = extension + wstring(L"\\DefaultIcon");

		wstring path=extension+
					L"\\shell\\"+
					action+
					L"\\command\\";
	
		//using | to be able to call all 3 functions even if true got returned
		//register the filetype extension
		refresh |= AddRegistry(HKEY_CURRENT_USER, extension.c_str(), desc.c_str())
		//register application association
		| AddRegistry(HKEY_CURRENT_USER, path.c_str(), app.c_str())
		//register icon for the filetype
		| AddRegistry(HKEY_CURRENT_USER, icon.c_str(), program);
	}
	if (refresh) {
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	}
}

bool Sys_IsOtherInstanceRunning(void) {
	mutex = CreateMutex(NULL, TRUE, PROGRAM_MUTEX);
	//prevent multiple instances if we are opening a demo
	if (ERROR_ALREADY_EXISTS == GetLastError()) {
		return true;
	}
	//in the other case just open another sintance
	return false;
}

#define SHAREDDATA_SIZE 512
#define MAPNAME PROGRAM_MUTEX"_Map"
HANDLE hMapFile;
bool Sys_CopySharedData(void *data, size_t size) {
	HANDLE hMapFile;
	LPCTSTR pBuf;

	hMapFile = CreateFileMapping(
					INVALID_HANDLE_VALUE,    // use paging file
					NULL,                    // default security
					PAGE_READWRITE,          // read/write access
					0,                       // maximum object size (high-order DWORD)
					SHAREDDATA_SIZE,         // maximum object size (low-order DWORD)
					MAPNAME); 
	if (hMapFile == NULL) {
		hMapFile = OpenFileMapping(
					FILE_MAP_ALL_ACCESS,   // read/write access
					FALSE,                 // do not inherit the name
					MAPNAME);  
		if (hMapFile == NULL) {
			return false;
		}
	}

	pBuf = (LPTSTR)MapViewOfFile(hMapFile,   // handle to map object
						FILE_MAP_ALL_ACCESS, // read/write permission
						0,
						0,
						SHAREDDATA_SIZE);
	if (pBuf == NULL) {
		return false;
	}

	CopyMemory((PVOID)pBuf, data, size);
	UnmapViewOfFile(pBuf);
//	CloseHandle(hMapFile);
	return true;
}

void *Sys_GetSharedData(void) {
	static CHAR ret[SHAREDDATA_SIZE];
	HANDLE hMapFile;
	LPCTSTR pBuf;

	hMapFile = OpenFileMapping(
					FILE_MAP_ALL_ACCESS,   // read/write access
					FALSE,                 // do not inherit the name
					MAPNAME);              // name of mapping object
	if (hMapFile == NULL) {
		return NULL;
	}

	pBuf = (LPTSTR)MapViewOfFile(hMapFile, // handle to map object
				FILE_MAP_ALL_ACCESS,  // read/write permission
				0,
				0,
				SHAREDDATA_SIZE);
	if (pBuf == NULL) {
		CloseHandle(hMapFile);
		return NULL;
	}

	wcstombs(ret, pBuf, sizeof(ret));
	UnmapViewOfFile(pBuf);
	CloseHandle(hMapFile);
	return ret;
}

extern UINT MSH_BROADCASTARGS;
extern void *Sys_GetSharedData(void);
extern void Com_ParseCommandLine(char *cmdLine);
extern qboolean Com_AddStartupCommands(void);
extern bool isSupported(const char *filename, dropLogic_t *out);
extern void switchMod(void);
void Sys_HandleEvent(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == MSH_BROADCASTARGS) {
		char *args = (char *)Sys_GetSharedData();
		if (args) {
			if (strstr(args, "+demo")) {
				switchMod();
			}
			Com_ParseCommandLine(args);
			Com_AddStartupCommands();
//			Cbuf_ExecuteText(EXEC_APPEND, args);
		}
		SwitchToThisWindow(hWnd, FALSE);
	 }/* else if (uMsg == WM_DROPFILES) {
		HDROP hDrop = (HDROP)wParam;
		TCHAR szFileName[MAX_PATH];
		DWORD dwCount = DragQueryFile(hDrop, 0xFFFFFFFF, szFileName, MAX_PATH);
		char fileName[MAX_PATH]; //expand size? but TCHAR can be just char if it's not unicode :s
		wcstombs(fileName, szFileName, sizeof(fileName));
		for (DWORD i = 0; i < dwCount; i++) {
			DragQueryFile(hDrop, 0, szFileName, MAX_PATH);
			dropLogic_t drop;
			if (!isSupported(fileName, &drop))
				continue;
			char cmd[MAX_PATH+16] = {0};
			Q_strcat(cmd, sizeof(cmd), drop.cmd);
			Q_strcat(cmd, sizeof(cmd), " \"");
			Q_strcat(cmd, sizeof(cmd), fileName);
			Q_strcat(cmd, sizeof(cmd), "\" ");
			if (drop.args)
				Q_strcat(cmd, sizeof(cmd), drop.args);
			if (!Q_stricmp(drop.cmd, "demo")) {
				switchMod();
			}
			Cbuf_ExecuteText(EXEC_APPEND, cmd);
			Cbuf_ExecuteText(EXEC_APPEND, "\n");
			if (!drop.allowMulti)
				break;
		}
		DragFinish(hDrop);
		SwitchToThisWindow(hWnd, FALSE);
	}*/
}
