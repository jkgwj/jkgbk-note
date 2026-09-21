#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook {
    // vagrants.exe 基址
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

    // ============== HOOK1 lstrcmpiA 结果判断跳转 (je -> jmp) ==============
    // 原始地址: 004EEC8B | 74 24 | je vagrants.4EECB1
    // 补丁效果: 74 24 → EB 24，无条件跳转
    BYTE g_DvdHook1Sig[2] = { 0x74, 0x24 };
    BYTE g_DvdHook1Patch[2] = { 0xEB, 0x24 };

    HookEntry g_DvdCheckHook1 = {
        0x000EEC8B,        // 偏移：004EEC8B - 00400000 = 000EEC8B
        g_DvdHook1Sig, 2,  // 特征码及长度
        { 0x000EEC8B },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DvdHook1Patch },// 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== HOOK2 GetDriveTypeA 判断跳转 (je -> jmp) ==============
    // 原始地址: 0051E884 | 74 02 | je vagrants.51E888
    // 补丁效果: 74 02 → EB 02，无条件跳转
    BYTE g_DvdHook2Sig[2] = { 0x74, 0x02 };
    BYTE g_DvdHook2Patch[2] = { 0xEB, 0x02 };

    HookEntry g_DvdCheckHook2 = {
        0x0011E884,        // 偏移：0051E884 - 00400000 = 0011E884
        g_DvdHook2Sig, 2,  // 特征码及长度
        { 0x0011E884 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DvdHook2Patch },// 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== HOOK3 test eax,eax 判断跳转 (jne -> jmp) ==============
    // 原始地址: 0051E8B7 | 75 05 | jne vagrants.51E8BE
    // 补丁效果: 75 05 → EB 05，无条件跳转
    BYTE g_DvdHook3Sig[2] = { 0x75, 0x05 };
    BYTE g_DvdHook3Patch[2] = { 0xEB, 0x05 };

    HookEntry g_DvdCheckHook3 = {
        0x0011E8B7,        // 偏移：0051E8B7 - 00400000 = 0011E8B7
        g_DvdHook3Sig, 2,  // 特征码及长度
        { 0x0011E8B7 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DvdHook3Patch },// 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"vagrants.exe");
        if (!g_BaseAddr)
            return 1;

        // 应用三个DVD检测HOOK
        ApplyHook(g_DvdCheckHook1);
        ApplyHook(g_DvdCheckHook2);
        ApplyHook(g_DvdCheckHook3);

        return 0;
    }

    void __stdcall UninstallAll()
    {
        // 重置完成标记
        g_DvdCheckHook1.done = false;
        g_DvdCheckHook2.done = false;
        g_DvdCheckHook3.done = false;
    }
}