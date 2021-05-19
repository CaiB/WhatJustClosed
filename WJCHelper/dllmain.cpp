// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <stdio.h>

#ifdef __cplusplus    // If used by C++ code, 
extern "C" {          // we need to export the C interface
#endif
#include <tchar.h>

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

    DWORD BytesWritten = 0;
    TCHAR Message[400]{};
    Message[0] = '\0';
    _stprintf_s(Message, 400, L"hWnd 0x%08X, PID %d, Title \"%s\", Process \"%s\"", wParam, ProcessID, WindowTitle, ProcessName);

    BOOL WriteResult = WriteFile(FileHandle, Message, (DWORD)(lstrlen(Message) + 1) * sizeof(TCHAR), &BytesWritten, (LPOVERLAPPED)NULL);
    if (!WriteResult)
    {
        printf("Failed to write to file with 0x%08X\n", GetLastError());
        return;
    }
}

LRESULT CALLBACK CBTCall(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_DESTROYWND)
    {
        // wParam is the window handle about to be destroyed
        WriteInfo(wParam);
    }

    return CallNextHookEx(MyHook, nCode, wParam, lParam);
}

// Call this second. Creates the hook to call back into the EXE on the desired events
__declspec(dllexport) HRESULT SetupHook(HINSTANCE dllInstance)
{
    MyHook = SetWindowsHookExA(WH_CBT, CBTCall, dllInstance, 0);
    if (MyHook == NULL) { return GetLastError(); }
    return S_OK;
}

// Call this when closing, stops the hook.
__declspec(dllexport) void CleanupHook()
{
    if (MyHook != NULL) { UnhookWindowsHookEx(MyHook); }
}

#ifdef __cplusplus
}
#endif