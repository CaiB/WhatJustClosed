#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include <winuser.h>
#include <strsafe.h>

// Forward declarations
int main(int argc, char* argv[]);
BOOL Start32Child(void);
void Stop32Child(void);
void DoReceive(void);
BOOL WINAPI CtrlHandler(DWORD eventType);
void Cleanup(void);


HINSTANCE HelperDLLHandle; // Our DLL instance, used to hook & unkook events
HANDLE ReceiverHandle; // The mailslot for receiving event data from other processes

HRESULT (*DLLSetupHook)(HINSTANCE);
void (*DLLCleanupHook)(void);

BOOL Continue = TRUE; // Whether to continue running, or exit
BOOL ShowInvisible = FALSE; // Whether to show entries for windows that closed while being invisible
BOOL Start32Bit = FALSE; // Whether to also start the 32 bit hook
BOOL OnlyHook = FALSE; // Whether to only add the hook, without creating/reading the mailslot

HANDLE ChildPipeIn, ChildProcess, ChildThread;
BOOL DoneCleanup = FALSE;

// Run with any arguments to set "all output" mode, showing both visible and invisible windows at time of closing.
// If no arguments are provided, then only windows that were visible will be output to the console.
int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (_stricmp(argv[i], "/?") == 0 || _stricmp(argv[i], "/help") == 0 || _stricmp(argv[i], "/h") == 0 || _stricmp(argv[i], "-?") == 0 || _stricmp(argv[i], "-help") == 0 || _stricmp(argv[i], "-h") == 0)
        {
            fputs("The 64-bit listener can only detect closing windows of 64-bit programs, and the 32-bit listener can only detect closing windows of 32-bit programs.\n", stdout);
            fputs("Available options:\n", stdout);
            fputs("  /32: Start the 32-bit listener as well\n", stdout);
            fputs("  /inv: Output info about windows that were invisible at the time of closing\n", stdout);
        }
        else if (_stricmp(argv[i], "/32") == 0 || _stricmp(argv[i], "-32") == 0) { Start32Bit = TRUE; }
        else if (_stricmp(argv[i], "/inv") == 0 || _stricmp(argv[i], "-inv") == 0) { ShowInvisible = TRUE; }
        else if (_stricmp(argv[i], "/hook") == 0 || _stricmp(argv[i], "-hook") == 0) { OnlyHook = TRUE; }
        else { printf("Console argument \"%s\" was not recognized.\n", argv[i]); }
    }
    Start32Bit &= (sizeof(void*) == 8); // Don't start a 32-bit subprocess if this is already the 32-bit version.
    
    if (!OnlyHook)
    {
        fputs("CaiB's WhatJustClosed v1.0, 2025-08-16\n", stdout);
        if (ShowInvisible) { fputs("(Running in all output mode) ", stdout); }
        fputs("Press Ctrl+C to stop & close.\n", stdout);

        // Create mailslot
        ReceiverHandle = CreateMailslotW(L"\\\\.\\mailslot\\WhatJustClosed", 0, MAILSLOT_WAIT_FOREVER, (LPSECURITY_ATTRIBUTES)NULL);
        if (ReceiverHandle == INVALID_HANDLE_VALUE || ReceiverHandle == NULL) { printf("[ERR] Failed to create mailslot: 0x%08X\n", (UINT32)GetLastError()); }
    }

    // Load DLL
    HelperDLLHandle = LoadLibraryW((sizeof(void*) == 4) ? L"WJCHelper_x86.dll" : L"WJCHelper_x64.dll");
    if (HelperDLLHandle == NULL)
    {
        DWORD ErrorCode = GetLastError();
        printf("[ERR] Failed to load WJCHelper library: 0x%08X\n", (UINT32)ErrorCode);
        return ErrorCode;
    }

    // Find and call SetupHook
    DLLSetupHook = (HRESULT(*)(HINSTANCE))GetProcAddress(HelperDLLHandle, "SetupHook");
    if (DLLSetupHook == NULL)
    {
        DWORD ErrorCode = GetLastError();
        printf("[ERR] Failed to get function pointer for setting up hook: 0x%08X\n", (UINT32)ErrorCode);
        return ErrorCode;
    }
    HRESULT HookCreateResult = (*DLLSetupHook)(HelperDLLHandle);
    if (FAILED(HookCreateResult))
    {
        printf("[ERR] Failed to create hook: 0x%08X\n", (UINT32)HookCreateResult);
        return HookCreateResult;
    }

    if (Start32Bit) { Start32Child(); }

    if (OnlyHook) { (void)_getch(); }
    else
    {
        SetConsoleCtrlHandler(CtrlHandler, TRUE);
        DoReceive();
    }

    if (Start32Bit) { Stop32Child(); }
    Cleanup();
}

BOOL Start32Child(void)
{
    HANDLE ChildInPipeRead, ChildInPipeWrite;
    SECURITY_ATTRIBUTES ChildSecurity =
    {
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .bInheritHandle = TRUE,
        .lpSecurityDescriptor = NULL
    };

    if (!CreatePipe(&ChildInPipeRead, &ChildInPipeWrite, &ChildSecurity, 0)) { fputs("[ERR] Failed to create in pipe for 32bit subprocess.\n", stdout); return FALSE; }
    if (!SetHandleInformation(ChildInPipeWrite, HANDLE_FLAG_INHERIT, 0)) { fputs("[ERR] Failed to set in pipe handle inherit mode for 32bit subprocess.\n", stdout); return FALSE; }

    PROCESS_INFORMATION ProcessInfo = { 0 };
    STARTUPINFO StartInfo = { 0 };
    StartInfo.cb = sizeof(STARTUPINFO);
    StartInfo.hStdInput = ChildInPipeRead;
    StartInfo.dwFlags |= STARTF_USESTDHANDLES;

    TCHAR ChildCommand[] = L"WhatJustClosed_x86.exe /hook";
    if (!CreateProcessW(NULL, ChildCommand, NULL, NULL, TRUE, 0, NULL, NULL, &StartInfo, &ProcessInfo)) { fputs("[ERR] Failed to start 32bit subprocess.\n", stdout); return FALSE; }
    ChildProcess = ProcessInfo.hProcess;
    ChildThread = ProcessInfo.hThread;
    ChildPipeIn = ChildInPipeWrite;
    return TRUE;
}

void Stop32Child(void)
{
    DWORD BytesWritten = 0;
    char WriteData = 'x';
    WriteFile(ChildPipeIn, &WriteData, 1, &BytesWritten, NULL);
    CloseHandle(ChildProcess);
    CloseHandle(ChildThread);
    CloseHandle(ChildPipeIn);
}

// Checks the mailslot for data, and outputs it to the console if available.
// Returns whether the operation succeeded (including if no data was available)
void DoReceive(void)
{
    HANDLE EventHandle = CreateEventW(NULL, FALSE, FALSE, L"WJCMailslot");
    if (EventHandle == NULL) { return; }

    OVERLAPPED OverlappedEvent = { 0 };
    OverlappedEvent.hEvent = EventHandle;

    DWORD MessageLength = 0, MessageCount = 0;
    const DWORD MessageBufferSize = 500;
    TCHAR* MessageBuffer = calloc(MessageBufferSize + 1, sizeof(TCHAR));
    if (MessageBuffer == NULL) { fputs("[ERR] Could not allocate message buffer", stdout); return; }

    while (Continue)
    {
        if (Start32Bit)
        {
            DWORD ChildProcExitCode = STILL_ACTIVE;
            if (GetExitCodeProcess(ChildProcess, &ChildProcExitCode) && ChildProcExitCode != STILL_ACTIVE)
            {
                printf("[INF] 32bit subprocess terminated with code %i, exiting.\n", (INT32)ChildProcExitCode);
                return;
            }
        }

        BOOL InfoResult = GetMailslotInfo(ReceiverHandle, (LPDWORD)NULL, &MessageLength, &MessageCount, (LPDWORD)NULL);
        if (!InfoResult) { printf("[ERR] Getting mailslot info failed with 0x%08X\n", (UINT32)GetLastError()); break; }

        if (MessageLength == MAILSLOT_NO_MESSAGE)
        {
            Sleep(100);
            continue;
        }

        while (MessageCount > 0)
        {
            DWORD AmountRead = 0;
            BOOL ReadResult = ReadFile(ReceiverHandle, MessageBuffer, min(MessageBufferSize, MessageLength), &AmountRead, &OverlappedEvent);
            if (!ReadResult) { printf("[ERR] Failed to read from mailslot with error 0x%08X\n", (UINT32)GetLastError()); return; }

            SYSTEMTIME Time;
            GetLocalTime(&Time);

            if (ShowInvisible || MessageBuffer[17] != L'I') { _tprintf(L"[%02d:%02d:%02d] Closed: %s\n", Time.wHour, Time.wMinute, Time.wSecond, MessageBuffer); }

            InfoResult = GetMailslotInfo(ReceiverHandle, (LPDWORD)NULL, &MessageLength, &MessageCount, (LPDWORD)NULL);
            if (!InfoResult) { printf("[ERR] Getting mailslot info failed with 0x%08X\n", (UINT32)GetLastError()); return; }
        }
    }
    CloseHandle(EventHandle);
}

// Handles the console closing, and unhooks the event before exiting.
BOOL WINAPI CtrlHandler(DWORD eventType)
{
    UNREFERENCED_PARAMETER(eventType);
    fputs("[INF] Exiting.\n", stdout);
    Cleanup();
    Continue = FALSE;
    return TRUE;
}

void Cleanup(void)
{
    if (DoneCleanup) { return; }
    // Find and call CleanupHook
    DLLCleanupHook = (void(*)(void))GetProcAddress(HelperDLLHandle, "CleanupHook");
    if (DLLSetupHook == NULL)
    {
        DWORD ErrorCode = GetLastError();
        printf("[ERR] Failed to get function pointer for cleanup: 0x%08X\n", (UINT32)ErrorCode);
        Continue = FALSE;
        return;
    }

    (*DLLCleanupHook)();
    DoneCleanup = TRUE;
}