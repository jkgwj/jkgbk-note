#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    //  基址
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
    BYTE g_DVDSig[8] = { 0x52, 0x88, 0x9C, 0x24, 0x20, 0x03, 0x00, 0x00 };    //52 88 9C 24 20 03 00 00
    BYTE g_DVDPatch[8] = { 0xE9, 0xB2 ,0x00,0x00,0x00,0x90,0x90,0x90 };                                      //E9 B2 00 00 00 90 90 90

    HookEntry g_DVDCheckHook = {
        0x000035FE,        // 特征码偏移 
        g_DVDSig, 8,       // 特征码及长度
        { 0x000035FE },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DVDPatch },    // 补丁数据
        { 8 },             // 补丁长度
        false              // 未完成标记
    };


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 レコンキスタ.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"レコンキスタ.exe");
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