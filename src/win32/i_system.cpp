// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//
//-----------------------------------------------------------------------------


#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <direct.h>
#include <string.h>
#include <process.h>

#include <stdarg.h>
#include <sys/types.h>
#include <sys/timeb.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "hardware.h"
#include "doomerrors.h"
#include <math.h>

#include "doomtype.h"
#include "version.h"
#include "doomdef.h"
#include "cmdlib.h"
#include "m_argv.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"
#include "i_music.h"
#include "resource.h"

#include "d_main.h"
#include "d_net.h"
#include "g_game.h"
#include "i_input.h"
#include "i_system.h"
#include "c_dispatch.h"
#include "templates.h"

#include "stats.h"

EXTERN_CVAR (String, language)

#ifdef USEASM
extern "C" void STACK_ARGS CheckMMX (CPUInfo *cpu);
#endif

extern "C"
{
	double		SecondsPerCycle = 1e-8;
	double		CyclesPerSecond = 1e8;		// 100 MHz
	CPUInfo		CPU;
}

extern HWND Window, ConWindow;
extern HINSTANCE g_hInst;

UINT TimerPeriod;
UINT TimerEventID;
UINT MillisecondsPerTic;
HANDLE NewTicArrived;
DWORD LanguageIDs[4];
void CalculateCPUSpeed ();

int (*I_GetTime) (bool saveMS);
int (*I_WaitForTic) (int);

os_t OSPlatform;

void I_Tactile (int on, int off, int total)
{
  // UNUSED.
  on = off = total = 0;
}

ticcmd_t emptycmd;
ticcmd_t *I_BaseTiccmd(void)
{
	return &emptycmd;
}

static DWORD basetime = 0;

// [RH] Returns time in milliseconds
unsigned int I_MSTime (void)
{
	DWORD tm;

	tm = timeGetTime();
	if (!basetime)
		basetime = tm;

	return tm - basetime;
}

static DWORD TicStart;
static DWORD TicNext;

//
// I_GetTime
// returns time in 1/35th second tics
//
int I_GetTimePolled (bool saveMS)
{
	DWORD tm;

	tm = timeGetTime();
	if (!basetime)
		basetime = tm;

	if (saveMS)
	{
		TicStart = tm;
		TicNext = (tm * TICRATE / 1000 + 1) * 1000 / TICRATE;
	}

	return ((tm-basetime)*TICRATE)/1000;
}

int I_WaitForTicPolled (int prevtic)
{
	int time;

	while ((time = I_GetTimePolled(false)) <= prevtic)
		;

	return time;
}


static int tics;
DWORD ted_start, ted_next;

int I_GetTimeEventDriven (bool saveMS)
{
	if (saveMS)
	{
		TicStart = ted_start;
		TicNext = ted_next;
	}
	return tics;
}

int I_WaitForTicEvent (int prevtic)
{
	while (prevtic >= tics)
	{
		WaitForSingleObject (NewTicArrived, 1000/TICRATE);
	}

	return tics;
}

void CALLBACK TimerTicked (UINT id, UINT msg, DWORD user, DWORD dw1, DWORD dw2)
{
	tics++;
	ted_start = timeGetTime ();
	ted_next = ted_start + MillisecondsPerTic;
	SetEvent (NewTicArrived);
}

// Returns the fractional amount of a tic passed since the most recent tic
fixed_t I_GetTimeFrac (DWORD *ms)
{
	DWORD now = timeGetTime();
	if (ms) *ms = TicNext;
	DWORD step = TicNext - TicStart;
	if (step == 0)
	{
		return FRACUNIT;
	}
	else
	{
		fixed_t frac = clamp<fixed_t> ((now - TicStart)*FRACUNIT/step, 0, FRACUNIT);
		return frac;
	}
}

void I_WaitVBL (int count)
{
	// I_WaitVBL is never used to actually synchronize to the
	// vertical blank. Instead, it's used for delay purposes.
	Sleep (1000 * count / 70);
}

// [RH] Detect the OS the game is running under
void I_DetectOS (void)
{
	OSVERSIONINFO info;
	const char *osname;

	info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx (&info);

	switch (info.dwPlatformId)
	{
	case VER_PLATFORM_WIN32s:
		OSPlatform = os_Win32s;
		osname = "3.x";
		break;

	case VER_PLATFORM_WIN32_WINDOWS:
		OSPlatform = os_Win95;
		if (info.dwMinorVersion < 10)
		{
			osname = "95";
		}
		else if (info.dwMinorVersion < 90)
		{
			osname = "98";
		}
		else
		{
			osname = "Me";
		}
		break;

	case VER_PLATFORM_WIN32_NT:
		OSPlatform = info.dwMajorVersion < 5 ? os_WinNT : os_Win2k;
		if (OSPlatform == os_WinNT)
		{
			osname = "NT";
		}
		else if (info.dwMinorVersion == 0)
		{
			osname = "2000";
		}
		else
		{
			osname = "XP";
		}
		break;

	default:
		OSPlatform = os_unknown;
		osname = "Unknown OS";
		break;
	}

	Printf ("OS: Windows %s %lu.%lu (Build %lu)\n",
			osname,
			info.dwMajorVersion, info.dwMinorVersion,
			OSPlatform == os_Win95 ? info.dwBuildNumber & 0xffff : info.dwBuildNumber);
	if (info.szCSDVersion[0])
	{
		Printf ("    %s\n", info.szCSDVersion);
	}

	if (OSPlatform == os_Win32s)
	{
		I_FatalError ("Sorry, Win32s is not supported.\n"
					  "Upgrade to a newer version of Windows.");
	}
	else if (OSPlatform == os_unknown)
	{
		Printf ("(Assuming Windows 95)\n");
		OSPlatform = os_Win95;
	}
}

//
// SubsetLanguageIDs
//
static void SubsetLanguageIDs (LCID id, LCTYPE type, int idx)
{
	char buf[8];
	LCID langid;
	char *idp;

	if (!GetLocaleInfo (id, type, buf, 8))
		return;
	langid = MAKELCID (strtoul(buf, NULL, 16), SORT_DEFAULT);
	if (!GetLocaleInfo (langid, LOCALE_SABBREVLANGNAME, buf, 8))
		return;
	idp = (char *)(&LanguageIDs[idx]);
	memset (idp, 0, 4);
	idp[0] = tolower(buf[0]);
	idp[1] = tolower(buf[1]);
	idp[2] = tolower(buf[2]);
	idp[3] = 0;
}

//
// SetLanguageIDs
//
void SetLanguageIDs ()
{
	size_t langlen = strlen (language);

	if (langlen < 2 || langlen > 3)
	{
		memset (LanguageIDs, 0, sizeof(LanguageIDs));
		SubsetLanguageIDs (LOCALE_USER_DEFAULT, LOCALE_ILANGUAGE, 0);
		SubsetLanguageIDs (LOCALE_USER_DEFAULT, LOCALE_IDEFAULTLANGUAGE, 1);
		SubsetLanguageIDs (LOCALE_SYSTEM_DEFAULT, LOCALE_ILANGUAGE, 2);
		SubsetLanguageIDs (LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTLANGUAGE, 3);
	}
	else
	{
		DWORD lang = 0;

		((BYTE *)&lang)[0] = (language)[0];
		((BYTE *)&lang)[1] = (language)[1];
		((BYTE *)&lang)[2] = (language)[2];
		LanguageIDs[0] = lang;
		LanguageIDs[1] = lang;
		LanguageIDs[2] = lang;
		LanguageIDs[3] = lang;
	}
}

//
// I_Init
//
void I_Init (void)
{
#ifndef USEASM
	memset (&CPU, 0, sizeof(CPU));
#else
	CheckMMX (&CPU);
	CalculateCPUSpeed ();

	// Why does Intel right-justify this string?
	char *f = CPU.CPUString, *t = f;

	while (*f == ' ')
	{
		++f;
	}
	if (f != t)
	{
		while (*f != 0)
		{
			*t++ = *f++;
		}
	}

#endif
	if (CPU.VendorID[0])
	{
		Printf ("CPU Vendor ID: %s\n", CPU.VendorID);
		if (CPU.CPUString[0])
		{
			Printf ("  Name: %s\n", CPU.CPUString);
		}
		if (CPU.bIsAMD)
		{
			Printf ("  Family %d (%d), Model %d, Stepping %d\n",
				CPU.Family, CPU.AMDFamily, CPU.AMDModel, CPU.AMDStepping);
		}
		else
		{
			Printf ("  Family %d, Model %d, Stepping %d\n",
				CPU.Family, CPU.Model, CPU.Stepping);
		}
		Printf ("  Features:");
		if (CPU.bMMX)		Printf (" MMX");
		if (CPU.bMMXPlus)	Printf (" MMX+");
		if (CPU.bSSE)		Printf (" SSE");
		if (CPU.bSSE2)		Printf (" SSE2");
		if (CPU.bSSE3)		Printf (" SSE3");
		if (CPU.b3DNow)		Printf (" 3DNow!");
		if (CPU.b3DNowPlus)	Printf (" 3DNow!+");
		Printf ("\n");
	}


	// Use a timer event if possible
	NewTicArrived = CreateEvent (NULL, FALSE, FALSE, NULL);
	if (NewTicArrived)
	{
		UINT delay;
		char *cmdDelay;

		cmdDelay = Args.CheckValue ("-timerdelay");
		delay = 0;
		if (cmdDelay != 0)
		{
			delay = atoi (cmdDelay);
		}
		if (delay == 0)
		{
			delay = 1000/TICRATE;
		}
		TimerEventID = timeSetEvent
			(
				delay,
				0,
				TimerTicked,
				0,
				TIME_PERIODIC
			);
		MillisecondsPerTic = delay;
	}
	if (TimerEventID != 0)
	{
		I_GetTime = I_GetTimeEventDriven;
		I_WaitForTic = I_WaitForTicEvent;
	}
	else
	{	// If no timer event, busy-loop with timeGetTime
		I_GetTime = I_GetTimePolled;
		I_WaitForTic = I_WaitForTicPolled;
	}

	atterm (I_ShutdownSound);
	I_InitSound ();
	I_InitInput (Window);
	I_InitHardware ();
}

void CalculateCPUSpeed ()
{
	LARGE_INTEGER freq;

	QueryPerformanceFrequency (&freq);

	if (freq.QuadPart != 0 && CPU.bRDTSC)
	{
		LARGE_INTEGER count1, count2;
		DWORD minDiff;
		cycle_t ClockCalibration = 0;

		// Count cycles for at least 55 milliseconds.
		// The performance counter is very low resolution compared to CPU
		// speeds today, so the longer we count, the more accurate our estimate.
		// On the other hand, we don't want to count too long, because we don't
		// want the user to notice us spend time here, since most users will
		// probably never use the performance statistics.
		minDiff = freq.LowPart * 11 / 200;

		// Minimize the chance of task switching during the testing by going very
		// high priority. This is another reason to avoid timing for too long.
		SetPriorityClass (GetCurrentProcess (), REALTIME_PRIORITY_CLASS);
		SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_TIME_CRITICAL);
		clock (ClockCalibration);
		QueryPerformanceCounter (&count1);
		do
		{
			QueryPerformanceCounter (&count2);
		} while ((DWORD)((unsigned __int64)count2.QuadPart - (unsigned __int64)count1.QuadPart) < minDiff);
		unclock (ClockCalibration);
		QueryPerformanceCounter (&count2);
		SetPriorityClass (GetCurrentProcess (), NORMAL_PRIORITY_CLASS);
		SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_NORMAL);

		CyclesPerSecond = (double)ClockCalibration *
			(double)freq.QuadPart /
			(double)((__int64)count2.QuadPart - (__int64)count1.QuadPart);
		SecondsPerCycle = 1.0 / CyclesPerSecond;
	}
	else
	{
		Printf ("Can't determine CPU speed, so pretending.\n");
	}

	Printf ("CPU Speed: %f MHz\n", CyclesPerSecond / 1e6);
}

//
// I_Quit
//
static int has_exited;

void STACK_ARGS I_Quit (void)
{
	has_exited = 1;		/* Prevent infinitely recursive exits -- killough */

	if (TimerEventID)
		timeKillEvent (TimerEventID);
	if (NewTicArrived)
		CloseHandle (NewTicArrived);

	timeEndPeriod (TimerPeriod);

	if (demorecording)
		G_CheckDemoStatus();
	G_ClearSnapshots ();
}


//
// I_Error
//
extern FILE *Logfile;
BOOL gameisdead;

void STACK_ARGS I_FatalError (const char *error, ...)
{
	static BOOL alreadyThrown = false;
	gameisdead = true;

	if (!alreadyThrown)		// ignore all but the first message -- killough
	{
		char errortext[MAX_ERRORTEXT];
		int index;
		va_list argptr;
		va_start (argptr, error);
		index = vsprintf (errortext, error, argptr);
// GetLastError() is usually useless because we don't do a lot of Win32 stuff
//		sprintf (errortext + index, "\nGetLastError = %ld", GetLastError());
		va_end (argptr);

		// Record error to log (if logging)
		if (Logfile)
			fprintf (Logfile, "\n**** DIED WITH FATAL ERROR:\n%s\n", errortext);

		throw CFatalError (errortext);
	}

	if (!has_exited)	// If it hasn't exited yet, exit now -- killough
	{
		has_exited = 1;	// Prevent infinitely recursive exits -- killough
		exit(-1);
	}
}

void STACK_ARGS I_Error (const char *error, ...)
{
	va_list argptr;
	char errortext[MAX_ERRORTEXT];

	va_start (argptr, error);
	vsprintf (errortext, error, argptr);
	va_end (argptr);

	throw CRecoverableError (errortext);
}

char DoomStartupTitle[256] = { 0 };

void I_SetTitleString (const char *title)
{
	int i;

	for (i = 0; title[i]; i++)
		DoomStartupTitle[i] = title[i];
}

void I_PrintStr (const char *cp, bool lineBreak)
{
	if (ConWindow == NULL)
		return;

	static bool newLine = true;
	HWND edit = (HWND)(LONG_PTR)GetWindowLongPtr (ConWindow, GWLP_USERDATA);
	char buf[256];
	int bpos = 0;

	SendMessage (edit, EM_SETSEL, (WPARAM)-1, 0);

	if (lineBreak && !newLine)
	{
		buf[0] = '\r';
		buf[1] = '\n';
		bpos = 2;
	}
	while (*cp != 0)
	{
		if (*cp == 28)
		{ // Skip color changes
			if (*++cp != 0)
				cp++;
			continue;
		}
		if (bpos < 253)
		{
			// Stupid edit controls need CR-LF pairs
			if (*cp == '\n')
			{
				buf[bpos++] = '\r';
			}
		}
		else
		{
			buf[bpos] = 0;
			SendMessage (edit, EM_REPLACESEL, FALSE, (LPARAM)buf);
			newLine = buf[bpos-1] == '\n';
			bpos = 0;
		}
		buf[bpos++] = *cp++;
	}
	if (bpos != 0)
	{
		buf[bpos] = 0;
		SendMessage (edit, EM_REPLACESEL, FALSE, (LPARAM)buf);
		newLine = buf[bpos-1] == '\n';
	}
}

EXTERN_CVAR (Bool, queryiwad);
static WadStuff *WadList;
static int NumWads;

static void SetQueryIWad (HWND dialog)
{
	HWND checkbox = GetDlgItem (dialog, IDC_DONTASKIWAD);
	int state = SendMessage (checkbox, BM_GETCHECK, 0, 0);

	queryiwad = (state != BST_CHECKED);
}

BOOL CALLBACK IWADBoxCallback (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND list;
	int i;

	switch (message)
	{
	case WM_INITDIALOG:
		list = GetDlgItem (hDlg, IDC_IWADLIST);
		for (i = 0; i < NumWads; i++)
		{
			char work[256];
			char *filepart = strrchr (WadList[i].Path, '/');
			if (filepart == NULL)
				filepart = WadList[i].Path;
			else
				filepart++;
			sprintf (work, "%s (%s)", IWADTypeNames[WadList[i].Type], filepart);
			SendMessage (list, LB_ADDSTRING, 0, (LPARAM)work);
			SendMessage (list, LB_SETITEMDATA, i, (LPARAM)i);
		}
		SendMessage (list, LB_SETCURSEL, 0, 0);
		SetFocus (list);
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog (hDlg, -1);
		}
		else if (LOWORD(wParam) == IDOK ||
			(LOWORD(wParam) == IDC_IWADLIST && HIWORD(wParam) == LBN_DBLCLK))
		{
			SetQueryIWad (hDlg);
			list = GetDlgItem (hDlg, IDC_IWADLIST);
			EndDialog (hDlg, SendMessage (list, LB_GETCURSEL, 0, 0));
		}
		break;
	}
	return FALSE;
}

int I_PickIWad (WadStuff *wads, int numwads)
{
	WadList = wads;
	NumWads = numwads;

	return DialogBox (g_hInst, MAKEINTRESOURCE(IDD_IWADDIALOG),
		(HWND)Window, (DLGPROC)IWADBoxCallback);
}

void *I_FindFirst (const char *filespec, findstate_t *fileinfo)
{
	return FindFirstFileA (filespec, (LPWIN32_FIND_DATAA)fileinfo);
}
int I_FindNext (void *handle, findstate_t *fileinfo)
{
	return !FindNextFileA ((HANDLE)handle, (LPWIN32_FIND_DATAA)fileinfo);
}

int I_FindClose (void *handle)
{
	return FindClose ((HANDLE)handle);
}
