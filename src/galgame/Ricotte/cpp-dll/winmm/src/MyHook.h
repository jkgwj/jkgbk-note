#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace MyHook
{
    // 补丁条目
    struct HookEntry
    {
        DWORD  sigOff;      // 特征码偏移（相对模块基址）
        BYTE*  sig;         // 特征码
        DWORD  sigLen;      // 特征码长度
        BYTE*  patchData;   // 补丁数据
        DWORD  patchLen;    // 补丁长度
        bool   done;        // 已完成
    };

    DWORD __stdcall InitHook(LPVOID lpParam);
    void  __stdcall UninstallAll();
}
