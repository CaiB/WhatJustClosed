// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <stdio.h>
#include <tchar.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

static HHOOK MyHook;

// Writes data into the mailslot for the main process to read
// Given an hWnd, this will gather the various bits of info about the process and window before writing to the mailbox.
void __stdcall WriteInfo(WPARAM wParam)
{
    HANDLE FileHandle;
    FileHandle = CreateFile(L"\\\\.\\mailslot\\WhatJustClosed", GENERIC_WRITE, FILE_SHARE_READ, (LPSECURITY_ATTRIBUTES)NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, (HANDLE)NULL);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        printf("Failed to create file with 0x%08X\n", GetLastError());
        return;
    }

    DWORD ProcessID = 0;
    GetWindowThreadProcessId((HWND)wParam, &ProcessID);

    TCHAR WindowTitle[50]{};
    GetWindowText((HWND)wParam, WindowTitle, 50);

    TCHAR ProcessName[100]{};
    DWORD Len = 100;
    QueryFullProcessImageNameW(OpenProcess(PROCESS_QUERY_INFORMATION, false, ProcessID), 0, ProcessName, &Len);

    BOOL WindowIsVisible = IsWindowVisible((HWND)wParam);

    DWORD BytesWritten = 0;
    TCHAR Message[400]{};
    Message[0] = '\0';
    _stprintf_s(Message, 400, L"hWnd 0x%08X, %s, PID %d, Title \"%s\", Process \"%s\"", wParam, (WindowIsVisible ? L"VIS" : L"INV"), ProcessID, WindowTitle, ProcessName);

    BOOL WriteResult = WriteFile(FileHandle, Message, (DWORD)(lstrlen(Message) + 1) * sizeof(TCHAR), &BytesWritten, (LPOVERLAPPED)NULL);
    if (!WriteResult)
    {
        printf("Failed to write to file with 0x%08X\n", GetLastError());
        return;
    }
}

// Called inside the process where a window is closing right before it closes
LRESULT CALLBACK CBTCall(int nCode, WPARAM wParam, LPARAM lParam)
{
    // wParam is the window handle about to be destroyed
    if (nCode == HCBT_DESTROYWND) { WriteInfo(wParam); }
    return CallNextHookEx(MyHook, nCode, wParam, lParam); // We need to do this so events will get forwarded on.
}

// Call this first. Creates the hook to ask the system to tell us about window close events
__declspec(dllexport) HRESULT SetupHook(HINSTANCE dllInstance)
{
    MyHook = SetWindowsHookExA(WH_CBT, CBTCall, dllInstance, 0);
    if (MyHook == NULL) { return GetLastError(); }
    return S_OK;
}

// Call this when closing, stops the hook. May cause problems if not cleaned up properly.
__declspec(dllexport) void CleanupHook()
{
    if (MyHook != NULL) { UnhookWindowsHookEx(MyHook); }
}

#ifdef __cplusplus
}
#endif