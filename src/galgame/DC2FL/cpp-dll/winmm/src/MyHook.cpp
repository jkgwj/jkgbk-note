#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // dc2fl.exe 基址
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

    // ============== 过DVD检测 ==============
    BYTE g_DVDSig[2] = { 0x74, 0x25 };
    BYTE g_DVDPatch[2] = { 0x90, 0x90 };

    HookEntry g_DVDCheckHook = {
        0x00015F54,        // 偏移：00415F54 - 00400000 = 15F54
        g_DVDSig, 2,       // 特征码+长度
        { 0x00015F54 },    // 补丁偏移
        { nullptr },       // 不校验原始指令
        { g_DVDPatch },    // 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== 过序列号验证 ==============
    BYTE g_SerialSig[6] = { 0x83, 0xEC, 0x18, 0xA1, 0x50, 0x54 };
    BYTE g_SerialPatch[8] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x90, 0x90 };

    HookEntry g_SerialCheckHook = {
        0x000258D0,        // 偏移：004258D0 - 00400000 = 258D0
        g_SerialSig, 6,    // 特征码+长度
        { 0x000258D0 },    // 补丁偏移
        { nullptr },       // 不校验原始指令
        { g_SerialPatch }, // 补丁数据
        { 8 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== 过KEY验证 ==============
    BYTE g_KeySig[6] = { 0x81, 0xEC, 0x08, 0x04, 0x00, 0x00 };
    BYTE g_KeyPatch[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };

    HookEntry g_KeyCheckHook = {
        0x00025A20,        // 偏移：00425A20 - 00400000 = 25A20
        g_KeySig, 6,       // 特征码+长度
        { 0x00025A20 },    // 补丁偏移
        { nullptr },       // 不校验原始指令
        { g_KeyPatch },    // 补丁数据
        { 6 },             // 补丁长度
        false              // 未完成标记
    };


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 dc2fl.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"dc2fl.exe");
        if (!g_BaseAddr)
            return 1;

        // 应用所有补丁
        ApplyHook(g_DVDCheckHook);
        ApplyHook(g_SerialCheckHook);
        ApplyHook(g_KeyCheckHook);

        return 0;
    }


    void __stdcall UninstallAll()
    {
        // 重置完成标记
        g_DVDCheckHook.done = false;
        g_SerialCheckHook.done = false;
        g_KeyCheckHook.done = false;
    }

}