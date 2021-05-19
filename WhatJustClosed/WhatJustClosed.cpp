#include <iostream>
#include <Windows.h>
#include <winuser.h>
#include <tchar.h>
#include <strsafe.h>

HINSTANCE HelperDLLHandle; // Our DLL instance, used to hook & unkook events
HANDLE ReceiverHandle; // The mailslot for receiving event data from other processes

HRESULT(*DLLSetupHook)(HINSTANCE);
void(*DLLCleanupHook)();

BOOL Continue = TRUE; // Whether to continue running, or exit
bool ShowInvisible = FALSE; // Whether to show entries for windows that closed while being invisible

// Sets up the mailslot so data can be sent to it.
void SetupReceiver()
{
    ReceiverHandle = CreateMailslot(L"\\\\.\\mailslot\\WhatJustClosed", 0, MAILSLOT_WAIT_FOREVER, (LPSECURITY_ATTRIBUTES)NULL);
    if (ReceiverHandle == INVALID_HANDLE_VALUE || ReceiverHandle == NULL)
    {
        printf("[ERR] Failed to create mailslot, error 0x%08X\n", GetLastError());
        return;
    }
}

// Checks the mailslot for data, and outputs it to the console if available.
// Returns whether the operation successed (including if no data was available)
BOOL DoReceive()
{
    HANDLE EventHandle = CreateEvent(NULL, FALSE, FALSE, L"WJCMailslot");
    if (EventHandle == NULL) { return FALSE; }

    OVERLAPPED OverlappedEvent{};
    OverlappedEvent.Offset = 0;
    OverlappedEvent.OffsetHigh = 0;
    OverlappedEvent.hEvent = EventHandle;

    DWORD MessageLength = 0, MessageCount = 0;
    BOOL InfoResult = GetMailslotInfo(ReceiverHandle, (LPDWORD)NULL, &MessageLength, &MessageCount, (LPDWORD)NULL);
    if (!InfoResult)
    {
        printf("[ERR] Getting mailslot info failed with 0x%08X\n", GetLastError());
        return FALSE;
    }

    if (MessageLength == MAILSLOT_NO_MESSAGE) { return TRUE; } // Nothing to read.

    DWORD MessageCountTotal = MessageCount;
    TCHAR Contents[400]{};
    DWORD AmountRead = 0;

    while (MessageCount != 0)
    {
        LPTSTR MessageContent = (LPTSTR)GlobalAlloc(GPTR, lstrlen((LPTSTR)Contents) * sizeof(TCHAR) + MessageLength);
        if (MessageContent == NULL) { return FALSE; }
        MessageContent[0] = '\0';

        BOOL ReadResult = ReadFile(ReceiverHandle, MessageContent, MessageLength, &AmountRead, &OverlappedEvent);
        if (!ReadResult)
        {
            printf("[ERR] Failed to read from mailslot with error 0x%08X\n", GetLastError());
            GlobalFree((HGLOBAL)MessageContent);
            return FALSE;
        }

        SYSTEMTIME Time;
        GetLocalTime(&Time);

        if (MessageContent[17] != L'I' || ShowInvisible)
        {
            _tprintf(L"[%02d:%02d:%02d] Closed: %s\n", Time.wHour, Time.wMinute, Time.wSecond, MessageContent);
        }

        GlobalFree((HGLOBAL)MessageContent);

        BOOL InfoResult = GetMailslotInfo(ReceiverHandle, (LPDWORD)NULL, &MessageLength, &MessageCount, (LPDWORD)NULL);
        if (!InfoResult)
        {
            printf("[ERR] Getting mailslot info failed with 0x%08X\n", GetLastError());
            return FALSE;
        }
    }

    CloseHandle(EventHandle);
    return TRUE;
}

// Handles the console closing, and unhooks the event before exiting.
BOOL WINAPI CtrlHandler(DWORD eventType)
{
    printf("Exiting.\n");
    // Find and call CleanupHook
    DLLCleanupHook = (void(*)())GetProcAddress(HelperDLLHandle, "CleanupHook");
    if (DLLSetupHook == NULL)
    {
        DWORD ErrorCode = GetLastError();
        printf("[ERR] Failed to get function pointer for cleaning up hook, error 0x%08X\n", ErrorCode);
        return ErrorCode;
    }

    (*DLLCleanupHook)();
    Continue = FALSE;
}

// Run with any arguments to set "all output" mode, showing both visible and invisible windows at time of closing.
// If no arguments are provided, then only windows that were visible will be output to the console.
int main(int argc, char* argv)
{
    ShowInvisible = (argc > 1);
    printf("CaiB's WhatJustClosed v0.2, 2021-05-18\n");
    if (ShowInvisible) { printf("Running in all output mode\n"); }
    printf("Press Ctrl+C to stop & close.\n");
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    // Create mailslot
    SetupReceiver();
    
    // Load DLL
    HelperDLLHandle = LoadLibrary(L"WJCHelper.dll");
    if (HelperDLLHandle == NULL)
    {
        DWORD ErrorCode = GetLastError();
        printf("[ERR] Failed to load library, error 0x%08X\n", ErrorCode);
        return ErrorCode;
    }

    // Find and call SetupHook
    DLLSetupHook = (HRESULT(*)(HINSTANCE))GetProcAddress(HelperDLLHandle, "SetupHook");
    if (DLLSetupHook == NULL)
    {
        DWORD ErrorCode = GetLastError();
        printf("[ERR] Failed to get function pointer for setting up hook, error 0x%08X\n", ErrorCode);
        return ErrorCode;
    }

    HRESULT HookCreateResult = (*DLLSetupHook)(HelperDLLHandle);
    if (FAILED(HookCreateResult))
    {
        printf("[ERR] Failed to create hook, error 0x%08X\n", HookCreateResult);
        return HookCreateResult;
    }

    // Receive and output data until we are closing.
    while (Continue)
    {
        BOOL RecRes = DoReceive();
        Sleep(100);
    }
}