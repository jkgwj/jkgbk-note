#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>


namespace MyHook
{
    extern DWORD g_BaseAddr;        // SiglusEngine.exe 基址 (运行时 GetModuleHandleW 获取)

    //AlphaRom (sarcheck.dll 入口) 
    extern DWORD  g_AlphaRomOffset; // 入口点偏移 = 0x012C0038 (相对基址)
    extern LPVOID g_TargetAddr;     // 入口点实际地址 = g_BaseAddr + g_AlphaRomOffset
    extern BYTE   g_Signature[70];  // 入口点特征码
    extern DWORD  g_SigLen;         // 特征码长度
    extern DWORD  g_PatchValue;     // 补丁 = 0x90000CC2 (ret 0xC)
    extern bool   g_AlphaRomPatched;// 是否已打补丁

    //区域检测
    extern DWORD g_RegionCheckOffset; // 偏移 = 0x00201DEF (相对基址)
    extern BYTE  g_RegionCheckSig[6]; // 原始指令: 0F 85 9B 00 00 00 (jne)
    extern BYTE  g_RegionCheckPatch[6];// 补丁:     E9 31 02 00 00 90 (jmp + nop)
    extern bool  g_RegionPatched;     // 是否已打补丁

    DWORD __stdcall InitHook(LPVOID lpParam);  // CreateThread 入口，获取基址 + 安装 Hook
    void  __stdcall PatchAlphaRom();           // 检查 AlphaRom 特征码，写入 ret 0xC
    void  __stdcall PatchRegionCheck();        // 检查区域检测特征码，写入 jmp
    void  __stdcall UninstallAll();            // 卸载 Hook
}
