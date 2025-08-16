#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <tchar.h>

// Forward declarations
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);
__declspec(dllexport) HRESULT SetupHook(HINSTANCE dllInstance);
__declspec(dllexport) void CleanupHook(void);
LRESULT CALLBACK CBTCall(int nCode, WPARAM wParam, LPARAM lParam);
void __stdcall WriteInfo(WPARAM wParam);


static HHOOK MyHook;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(ul_reason_for_call);
    UNREFERENCED_PARAMETER(lpReserved);
    return TRUE;
}

// Call this first. Creates the hook to ask the system to tell us about window close events
__declspec(dllexport) HRESULT SetupHook(HINSTANCE dllInstance)
{
    MyHook = SetWindowsHookExW(WH_CBT, CBTCall, dllInstance, 0);
    if (MyHook == NULL) { return GetLastError(); }
    return S_OK;
}

// Call this when closing, stops the hook. May cause problems if not cleaned up properly.
__declspec(dllexport) void CleanupHook(void)
{
    if (MyHook != NULL) { UnhookWindowsHookEx(MyHook); }
}

// Called inside the process where a window is closing right before it closes
LRESULT CALLBACK CBTCall(int nCode, WPARAM wParam, LPARAM lParam)
{
    // wParam is the window handle about to be destroyed
    if (nCode == HCBT_DESTROYWND) { WriteInfo(wParam); }
    return CallNextHookEx(MyHook, nCode, wParam, lParam); // We need to do this so events will get forwarded on.
}

// Writes data into the mailslot for the main process to read
// Given an hWnd, this will gather the various bits of info about the process and window before writing to the mailbox.
void __stdcall WriteInfo(WPARAM wParam)
{
    HANDLE FileHandle;
    FileHandle = CreateFileW(L"\\\\.\\mailslot\\WhatJustClosed", GENERIC_WRITE, FILE_SHARE_READ, (LPSECURITY_ATTRIBUTES)NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, (HANDLE)NULL);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        printf("[WhatJustClosed] Failed to open mailslot: 0x%08X\n", (UINT32)GetLastError());
        return;
    }

    DWORD ProcessID = 0;
    GetWindowThreadProcessId((HWND)wParam, &ProcessID);

    TCHAR WindowTitle[50];
    if (GetWindowTextW((HWND)wParam, WindowTitle, 50) <= 0) { WindowTitle[0] = '\0'; }

    TCHAR ProcessName[100];
    DWORD ProcessNameLen = 100;
    if (QueryFullProcessImageNameW(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ProcessID), 0, ProcessName, &ProcessNameLen) == 0) { ProcessName[0] = '\0'; }

    BOOL WindowIsVisible = IsWindowVisible((HWND)wParam);

    DWORD BytesWritten = 0;
    TCHAR Message[400] = { 0 };
    swprintf_s(Message, 400, L"hWnd 0x%08X, %s, PID %d, %s, Title \"%s\", Process \"%s\"", (UINT32)wParam, (WindowIsVisible ? L"VIS" : L"INV"), (UINT32)ProcessID, (sizeof(void*) == 4 ? L"32b" : L"64b"), WindowTitle, ProcessName);
    int MessageLen = lstrlenW(Message);

    BOOL WriteResult = WriteFile(FileHandle, Message, (MessageLen + 1) * sizeof(TCHAR), &BytesWritten, (LPOVERLAPPED)NULL);
    if (!WriteResult)
    {
        printf("[WhatJustClosed] Failed to write to mailslot: 0x%08X\n", (UINT32)GetLastError());
        return;
    }
}