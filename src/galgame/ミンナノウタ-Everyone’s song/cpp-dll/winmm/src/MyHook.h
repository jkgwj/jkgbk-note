#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>


namespace MyHook
{
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
        int    patch_mod;         // 断点模式（0=直接写内存，1=断点/Detour） -- 改为 int，移除默认值以避免聚合初始化问题
    };

    //通用工具函数
    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len);  // VirtualQuery + memcmp
    void WritePatch(DWORD offset, BYTE* data, DWORD len);      // VirtualProtect + memcpy
    void ApplyHook(HookEntry& hk, const char* name);             // 校验特征码后写入全部补丁

    // 对外接口 
    DWORD __stdcall InitHook(LPVOID lpParam);  // 获取基址 + 安装 Hook
    void  __stdcall PatchAlphaRom();           // 过AlphaRom
    void  __stdcall UninstallAll();            // 卸载 Hook
}