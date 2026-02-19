// version.dll proxy - forwards all exports to the real system version.dll
#pragma once
#include <windows.h>

// Original function pointers
static HMODULE g_realVersionDll = NULL;

typedef BOOL(WINAPI* fn_GetFileVersionInfoA)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL(WINAPI* fn_GetFileVersionInfoByHandle)(DWORD, LPCWSTR, DWORD, LPVOID);
typedef BOOL(WINAPI* fn_GetFileVersionInfoExA)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL(WINAPI* fn_GetFileVersionInfoExW)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD(WINAPI* fn_GetFileVersionInfoSizeA)(LPCSTR, LPDWORD);
typedef DWORD(WINAPI* fn_GetFileVersionInfoSizeExA)(DWORD, LPCSTR, LPDWORD);
typedef DWORD(WINAPI* fn_GetFileVersionInfoSizeExW)(DWORD, LPCWSTR, LPDWORD);
typedef DWORD(WINAPI* fn_GetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
typedef BOOL(WINAPI* fn_GetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD(WINAPI* fn_VerFindFileA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
typedef DWORD(WINAPI* fn_VerFindFileW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
typedef DWORD(WINAPI* fn_VerInstallFileA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
typedef DWORD(WINAPI* fn_VerInstallFileW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
typedef BOOL(WINAPI* fn_VerQueryValueA)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL(WINAPI* fn_VerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

static fn_GetFileVersionInfoA       o_GetFileVersionInfoA;
static fn_GetFileVersionInfoByHandle o_GetFileVersionInfoByHandle;
static fn_GetFileVersionInfoExA     o_GetFileVersionInfoExA;
static fn_GetFileVersionInfoExW     o_GetFileVersionInfoExW;
static fn_GetFileVersionInfoSizeA   o_GetFileVersionInfoSizeA;
static fn_GetFileVersionInfoSizeExA o_GetFileVersionInfoSizeExA;
static fn_GetFileVersionInfoSizeExW o_GetFileVersionInfoSizeExW;
static fn_GetFileVersionInfoSizeW   o_GetFileVersionInfoSizeW;
static fn_GetFileVersionInfoW       o_GetFileVersionInfoW;
static fn_VerFindFileA              o_VerFindFileA;
static fn_VerFindFileW              o_VerFindFileW;
static fn_VerInstallFileA           o_VerInstallFileA;
static fn_VerInstallFileW           o_VerInstallFileW;
static fn_VerQueryValueA            o_VerQueryValueA;
static fn_VerQueryValueW            o_VerQueryValueW;

static BOOL LoadRealVersionDll() {
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat_s(sysDir, MAX_PATH, "\\version.dll");
    g_realVersionDll = LoadLibraryA(sysDir);
    if (!g_realVersionDll) return FALSE;

    o_GetFileVersionInfoA       = (fn_GetFileVersionInfoA)GetProcAddress(g_realVersionDll, "GetFileVersionInfoA");
    o_GetFileVersionInfoByHandle = (fn_GetFileVersionInfoByHandle)GetProcAddress(g_realVersionDll, "GetFileVersionInfoByHandle");
    o_GetFileVersionInfoExA     = (fn_GetFileVersionInfoExA)GetProcAddress(g_realVersionDll, "GetFileVersionInfoExA");
    o_GetFileVersionInfoExW     = (fn_GetFileVersionInfoExW)GetProcAddress(g_realVersionDll, "GetFileVersionInfoExW");
    o_GetFileVersionInfoSizeA   = (fn_GetFileVersionInfoSizeA)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeA");
    o_GetFileVersionInfoSizeExA = (fn_GetFileVersionInfoSizeExA)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeExA");
    o_GetFileVersionInfoSizeExW = (fn_GetFileVersionInfoSizeExW)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeExW");
    o_GetFileVersionInfoSizeW   = (fn_GetFileVersionInfoSizeW)GetProcAddress(g_realVersionDll, "GetFileVersionInfoSizeW");
    o_GetFileVersionInfoW       = (fn_GetFileVersionInfoW)GetProcAddress(g_realVersionDll, "GetFileVersionInfoW");
    o_VerFindFileA              = (fn_VerFindFileA)GetProcAddress(g_realVersionDll, "VerFindFileA");
    o_VerFindFileW              = (fn_VerFindFileW)GetProcAddress(g_realVersionDll, "VerFindFileW");
    o_VerInstallFileA           = (fn_VerInstallFileA)GetProcAddress(g_realVersionDll, "VerInstallFileA");
    o_VerInstallFileW           = (fn_VerInstallFileW)GetProcAddress(g_realVersionDll, "VerInstallFileW");
    o_VerQueryValueA            = (fn_VerQueryValueA)GetProcAddress(g_realVersionDll, "VerQueryValueA");
    o_VerQueryValueW            = (fn_VerQueryValueW)GetProcAddress(g_realVersionDll, "VerQueryValueW");
    return TRUE;
}
