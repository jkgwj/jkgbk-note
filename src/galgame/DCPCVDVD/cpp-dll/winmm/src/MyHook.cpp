#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // dcpcv.exe 基址
    DWORD g_BaseAddr = 0;


    // 校验特征码
    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(addr, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        return (::memcmp(addr, sig, len) == 0);
    }

    // 写入补丁
    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return;
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    // 应用 Hook
    void ApplyHook(HookEntry& hk)
    {
        if (hk.done)
            return;
        if (!VerifySignature(hk.sigOff, hk.sig, hk.sigLen))
            return;
        for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
        {
            if (hk.patchOrig[i])
            {
                if (!VerifySignature(hk.patchOff[i], hk.patchOrig[i], hk.patchLen[i]))
                    return;  // 补丁点原始指令不匹配, 放弃全部
            }
            WritePatch(hk.patchOff[i], hk.patchData[i], hk.patchLen[i]);
        }
        hk.done = true;
    }

    // Hook DVD/CD检测
    BYTE g_DVDSig[2] = { 0x74, 0x33 };
    BYTE g_DVDPatch[2] = { 0x90, 0x90 };

    HookEntry g_DVDCheckHook = {
        0x0000EE63,        // 偏移：0040EE63 - 00400000 = 0000EE63
        g_DVDSig, 2,       // 特征码及长度
        { 0x0000EE63 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DVDPatch },    // 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    // Hook 序列号验证
    BYTE g_SerialSig[7] = { 0x8B, 0x44, 0x24, 0x08, 0x83, 0xEC, 0x14 };
    BYTE g_SerialPatch[7] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x90 };

    HookEntry g_SerialCheckHook = {
        0x0001B9B0,        // 偏移：0041B9B0 - 00400000 = 0001B9B0
        g_SerialSig, 7,    // 特征码及长度
        { 0x0001B9B0 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_SerialPatch }, // 补丁数据
        { 7 },             // 补丁长度
        false              // 未完成标记
    };


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 dcpcv.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"dcpcv.exe");
        if (!g_BaseAddr)
            return 1;

        // 直接检测特征码并应用Hook
        ApplyHook(g_DVDCheckHook);
        ApplyHook(g_SerialCheckHook);

        return 0;
    }


    void __stdcall UninstallAll()
    {
        // 重置完成标记 (静态补丁无法直接恢复，如需恢复需保存原始字节)
        g_DVDCheckHook.done = false;
        g_SerialCheckHook.done = false;
    }

}