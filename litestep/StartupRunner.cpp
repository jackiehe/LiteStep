/*
This is a part of the LiteStep Shell Source code.

Copyright (C) 1997-2002 The LiteStep Development Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/ 
/****************************************************************************
****************************************************************************/
#include "StartupRunner.h"
#include "../utility/shellhlp.h"
#include <regstr.h>
#include "../utility/core.hpp"

#define ERK_NONE				0x0000
#define ERK_RUNSUBKEYS			0x0001 // runs key and its subkeys
#define ERK_DELETE				0x0002
#define REGSTR_PATH_RUN_POLICY	REGSTR_PATH_POLICIES _T("\\Explorer\\Run")

StartupRunner::StartupRunner()
{}

StartupRunner::~StartupRunner()
{}

DWORD StartupRunner::Run(void* pvVoid)
{
    bool bRunStartup = _IsFirstRunThisSession();

    // by keeping the call to _IsFirstRunThisSession() above we make sure the
    // regkey is created even if we're in "force startup" mode
    if (bRunStartup || (BOOL)pvVoid == TRUE)
	{
		bool bHKLMRun = !SHRestricted(REST_NOLOCALMACHINERUN);
		bool bHKCURun = !SHRestricted(REST_NOCURRENTUSERRUN);
		bool bHKLMRunOnce = !SHRestricted(REST_NOLOCALMACHINERUNONCE);
		bool bHKCURunOnce = !SHRestricted(REST_NOCURRENTUSERRUNONCE);

		// Ini Sections

		// HKLM Keys
		_RunRegKeys(HKEY_LOCAL_MACHINE, REGSTR_PATH_RUN_POLICY, ERK_NONE);
		if (bHKLMRun)
		{
			_RunRegKeys(HKEY_LOCAL_MACHINE, REGSTR_PATH_RUN, ERK_NONE);
		}

		// HKCU Keys
		_RunRegKeys(HKEY_CURRENT_USER, REGSTR_PATH_RUN_POLICY, ERK_NONE);
		if (bHKCURun)
		{
			_RunRegKeys(HKEY_CURRENT_USER, REGSTR_PATH_RUN, ERK_NONE);
		}

        _RunStartupMenu();
        
        if (bHKLMRunOnce)
        {
            _RunRegKeys(HKEY_LOCAL_MACHINE, REGSTR_PATH_RUNONCE,
                (ERK_RUNSUBKEYS | ERK_DELETE));
        }
        
        if (bHKCURunOnce)
        {
            _RunRegKeys(HKEY_CURRENT_USER, REGSTR_PATH_RUNONCE,
                (ERK_RUNSUBKEYS | ERK_DELETE));
        }
        
        _RunRegKeys(HKEY_LOCAL_MACHINE, REGSTR_PATH_RUN_POLICY, ERK_RUNSUBKEYS);
        if (bHKLMRun)
        {
            _RunRegKeys(HKEY_LOCAL_MACHINE, REGSTR_PATH_RUN, ERK_RUNSUBKEYS);
        }
        
        _RunRegKeys(HKEY_CURRENT_USER, REGSTR_PATH_RUN_POLICY, ERK_RUNSUBKEYS);
        if (bHKCURun)
        {
            _RunRegKeys(HKEY_CURRENT_USER, REGSTR_PATH_RUN, ERK_RUNSUBKEYS);
        }
    }
    
    //
    // RunOnceEx
    //
    // http://support.microsoft.com/default.aspx?scid=kb;EN-US;q232509
    // http://support.microsoft.com/default.aspx?scid=kb;en-us;Q232487
    //
    char szArgs[MAX_PATH] = { 0 };
    UINT uChars = GetSystemDirectory(szArgs, MAX_PATH);
    
    if (uChars > 0 && uChars < MAX_PATH)
    {
        if (SUCCEEDED(StringCchCat(szArgs, MAX_PATH,
            "\\iernonce.dll,RunOnceExProcess")))
        {
            ShellExecute(NULL, "open", "rundll32.exe", szArgs, NULL, SW_NORMAL);
        }
    }
    
	return bRunStartup;
}

void StartupRunner::_RunStartupMenu()
{
	const UINT STARTUPMENU_TABLE[] =
    {
        CSIDL_COMMON_STARTUP,
        CSIDL_COMMON_ALTSTARTUP,
        CSIDL_STARTUP,
        CSIDL_ALTSTARTUP
    };
	const ULONG STARTUPMENU_SIZE = sizeof(STARTUPMENU_TABLE) / sizeof(UINT);

	TCHAR tzPath[MAX_PATH];

	for (int i = 0; i < STARTUPMENU_SIZE; ++i)
	{
		if (GetShellFolderPath(STARTUPMENU_TABLE[i], tzPath, MAX_PATH))
		{
			_RunFolderContents(tzPath);
		}
	}
}

void StartupRunner::_RunFolderContents(LPCTSTR ptzPath)
{
	TCHAR tzSearchPath[MAX_PATH];
	WIN32_FIND_DATA findData;
	HANDLE hSearch;

	if (IsValidStringPtr(ptzPath) && (ptzPath[0] != _T('\0')))
	{
		StringCchCopy(tzSearchPath, MAX_PATH, ptzPath);
		PathAppend(tzSearchPath, _T("*.*"));

		hSearch = FindFirstFile(tzSearchPath, &findData);
		while (hSearch != INVALID_HANDLE_VALUE)
		{
			if (!PathIsDirectory(findData.cFileName) &&
                !(findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) &&
                !(findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
			{
                SHELLEXECUTEINFO seiCommand = { 0 };
				seiCommand.cbSize = sizeof(SHELLEXECUTEINFO);

				seiCommand.lpFile = findData.cFileName;
				seiCommand.lpDirectory = ptzPath;
				seiCommand.nShow = SW_SHOWNORMAL;
				seiCommand.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_NO_UI;

                ShellExecuteEx(&seiCommand);
			}

			if (!FindNextFile(hSearch, &findData))
			{
				FindClose(hSearch);
				hSearch = INVALID_HANDLE_VALUE;
			}
		}
	}
}


//
// IsFirstRunThisSession()
//
bool StartupRunner::_IsFirstRunThisSession()
{
	bool bReturn = false;
	HKEY hkExplorer;
	TCHAR tzSessionInfo[30];

	OSVERSIONINFO OsVersionInfo;
	OsVersionInfo.dwOSVersionInfoSize = sizeof(OsVersionInfo);
	GetVersionEx(&OsVersionInfo);

	// On NT systems, the SessionInfo subkey will be the AuthenticationID
	if (OsVersionInfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
	{
		HANDLE hToken;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken) != 0)
		{
			TOKEN_STATISTICS tsStats;
			DWORD dwOutSize;

			if (GetTokenInformation(hToken, TokenStatistics,
                &tsStats, sizeof(tsStats), &dwOutSize))
			{
				StringCchPrintf(tzSessionInfo, 30, _T("SessionInfo\\%08x%08x"),
                    tsStats.AuthenticationId.HighPart,
                    tsStats.AuthenticationId.LowPart);

				// Create the SessionInfo and StartUpHasBeenRun keys
				LONG lResult = RegCreateKeyEx(HKEY_CURRENT_USER,
                    REGSTR_PATH_EXPLORER, 0, NULL, REG_OPTION_NON_VOLATILE,
                    MAXIMUM_ALLOWED, NULL, &hkExplorer, NULL);

				if (lResult == ERROR_SUCCESS)
				{
					HKEY hkSessionInfo;

					lResult = RegCreateKeyEx(hkExplorer, tzSessionInfo, 0, NULL,
                        REG_OPTION_VOLATILE, KEY_WRITE, NULL, &hkSessionInfo,
                        NULL);

					if (lResult == ERROR_SUCCESS)
					{
						DWORD dwDisposition;
						HKEY hkStartup;
						lResult = RegCreateKeyEx(hkSessionInfo,
                            _T("StartupHasBeenRun"), 0, NULL,
                            REG_OPTION_VOLATILE, KEY_WRITE, NULL,
                            &hkStartup, &dwDisposition);

                        RegCloseKey(hkStartup);

                        if (dwDisposition == REG_CREATED_NEW_KEY)
						{
							bReturn = true;
						}
					}

                    RegCloseKey(hkSessionInfo);
				}

                RegCloseKey(hkExplorer);
			}
		}

        CloseHandle(hToken);
	}
	else
	{
		bReturn = true;
	}

	return bReturn;
}


//
// RunRegKeys(HKEY hkParent, LPCSTR pszSubKey, DWORD dwFlags)
//
void StartupRunner::_RunRegKeys(HKEY hkParent, LPCTSTR ptzSubKey, DWORD dwFlags)
{
    HKEY hkSubKey;

	LONG lResult = RegOpenKeyEx(hkParent, ptzSubKey, 0, MAXIMUM_ALLOWED,
        &hkSubKey);

	if (lResult == ERROR_SUCCESS)
	{
        //
        // Run the key itself
        //
        for (DWORD dwLoop = 0; ; ++dwLoop)
        {
            TCHAR tzNameBuffer[MAX_PATH] = { 0 };
            TCHAR tzValueBuffer[MAX_LINE_LENGTH] = { 0 };
            
            DWORD dwNameSize = MAX_PATH;
            DWORD dwValueSize = MAX_LINE_LENGTH;
            DWORD dwType;

            lResult = RegEnumValue(hkSubKey, dwLoop, tzNameBuffer, &dwNameSize,
                NULL, &dwType, (LPBYTE)tzValueBuffer, &dwValueSize);
            
            if (lResult == ERROR_MORE_DATA)
            {
                // tzNameBuffer too small?
                continue;
            }
            else if (lResult == ERROR_SUCCESS)
            {
                if ((dwType == REG_SZ) || (dwType == REG_EXPAND_SZ))
                {
                    STARTUPINFO suInfo = { 0 };
                    PROCESS_INFORMATION procInfo;
                    
                    suInfo.cb = sizeof(suInfo);
                    
                    BOOL bReturn = CreateProcess(NULL, tzValueBuffer,
                        NULL, NULL, FALSE,
                        CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                        NULL, NULL, &suInfo,&procInfo);
                    
                    if (bReturn)
                    {
                        CloseHandle(procInfo.hProcess);
                        CloseHandle(procInfo.hThread);
                    }
                    
                    if ((dwFlags & ERK_DELETE) && (tzNameBuffer[0] != _T('!')))
                    {
                        if (RegDeleteValue(hkSubKey, tzNameBuffer) ==
                            ERROR_SUCCESS)
                        {
                            --dwLoop;
                        }
                    }
                }
            }
            else
            {
                break;
            }
        }

        //
        // Run subkeys as well?
        //
        if (dwFlags & ERK_RUNSUBKEYS)
		{
            dwFlags &= ~(ERK_RUNSUBKEYS);

			for (DWORD dwLoop = 0; ; ++dwLoop)
			{
                TCHAR tzNameBuffer[MAX_PATH] = { 0 };
                lResult = RegEnumKey(hkSubKey, dwLoop, tzNameBuffer, MAX_PATH);
				
                if (lResult == ERROR_MORE_DATA)
				{
					// tzNameBuffer too small?
                    continue;
				}
				else if (lResult == ERROR_SUCCESS)
				{
					_RunRegKeys(hkSubKey, tzNameBuffer, dwFlags);

					if (dwFlags & ERK_DELETE)
					{
						if (RegDeleteValue(hkSubKey, tzNameBuffer) ==
                            ERROR_SUCCESS)
						{
							--dwLoop;
						}
					}
				}
				else
				{
					break;
				}
			}
		}
	}

    RegCloseKey(hkSubKey);
}
