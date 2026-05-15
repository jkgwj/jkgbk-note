#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // そらいろ.exe 基址
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

    // Hook DVD检测
    BYTE g_DVDSig[2] = { 0x75, 0x77 };
    BYTE g_DVDPatch[2] = { 0xEB, 0x77 };

    HookEntry g_DVDCheckHook = {
        0x000039E0,        // 特征码偏移 (004039E0 - 00400000)
        g_DVDSig, 2,       // 特征码及长度
        { 0x000039E0 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DVDPatch },    // 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 そらいろ.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"そらいろ.exe");
        if (!g_BaseAddr)
            return 1;

        // 直接检测特征码并应用Hook
        ApplyHook(g_DVDCheckHook);

        return 0;
    }


    void __stdcall UninstallAll()
    {
        // 重置完成标记 (静态补丁无法直接恢复，如需恢复需保存原始字节)
        g_DVDCheckHook.done = false;
    }

}