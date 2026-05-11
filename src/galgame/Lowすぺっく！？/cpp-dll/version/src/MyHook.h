#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>


namespace MyHook
{
    extern DWORD g_BaseAddr;        // SiglusEngine.exe 基址 

    // AlphaRom (sarcheck.dll 入口)
    extern DWORD  g_AlphaRomOffset; // 入口点偏移 
    extern LPVOID g_TargetAddr;     // 入口点实际地址 = g_BaseAddr + g_AlphaRomOffset
    extern BYTE   g_Signature[70];  // 入口点特征码
    extern DWORD  g_SigLen;         // 特征码长度
    extern DWORD  g_PatchValue;     // 补丁 
    extern bool   g_AlphaRomPatched;// 是否已打补丁

    // 通用 Hook 点结构 
    struct HookEntry
    {
        DWORD  sigOff;         // 特征码偏移
        BYTE*  sig;            // 特征码
        DWORD  sigLen;         // 特征码长度
        DWORD  patchOff[3];    // 补丁偏移
        BYTE*  patchOrig[3];   // 补丁处原始指令 (nullptr = 跳过校验)
        BYTE*  patchData[3];   // 补丁数据
        DWORD  patchLen[3];    // 补丁长度
        bool   done;           // 已完成标记
    };

    extern HookEntry g_RegionHook;  // 区域检测
    extern HookEntry g_DVDHook;     // DVD检测

    //通用工具函数
    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len);  // VirtualQuery + memcmp
    void WritePatch(DWORD offset, BYTE* data, DWORD len);      // VirtualProtect + memcpy
    void ApplyHook(HookEntry& hk);                              // 校验特征码后写入全部补丁

    // 对外接口 
    DWORD __stdcall InitHook(LPVOID lpParam);  // 获取基址 + 安装 Hook
    void  __stdcall PatchAlphaRom();           // 过AlphaRom
    void  __stdcall UninstallAll();            // 卸载 Hook
}
