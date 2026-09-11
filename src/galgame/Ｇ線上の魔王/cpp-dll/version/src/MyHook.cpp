#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook {
    // gstring.exe 基址
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

    // ============== HOOK1 光驱类型检测跳转 ==============
    // 原始地址: 0060AC95 | 0F 85 C7 00 00 00 | jne 非光驱则跳过
    // 补丁效果: NOP填充, 强制进入光驱处理分支
    BYTE g_DvdHook1Sig[6] = { 0x0F, 0x85, 0xC7, 0x00, 0x00, 0x00 };
    BYTE g_DvdHook1Patch[6] = { 0x90,0x90,0x90,0x90,0x90,0x90 };

    HookEntry g_DvdCheckHook1 = {
        0x0020AC95,        // 偏移：0060AC95 - 00400000 = 0020AC95
        g_DvdHook1Sig, 6,     // 特征码及长度
        { 0x0020AC95 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DvdHook1Patch },  // 补丁数据
        { 6 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== HOOK2 光盘卷信息检测跳转 ==============
    // 原始地址: 0060AD0E | 74 52 | je 读盘失败则跳过
    // 补丁效果: NOP填充, 强制认定光盘读取成功
    BYTE g_DvdHook2Sig[2] = { 0x74, 0x52 };
    BYTE g_DvdHook2Patch[2] = { 0x90,0x90 };

    HookEntry g_DvdCheckHook2 = {
        0x0020AD0E,        // 偏移：0060AD0E - 00400000 = 0020AD0E
        g_DvdHook2Sig, 2,     // 特征码及长度
        { 0x0020AD0E },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_DvdHook2Patch },  // 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"gstring.exe");
        if (!g_BaseAddr)
            return 1;

        // 应用两个DVD检测HOOK
        ApplyHook(g_DvdCheckHook1);
        ApplyHook(g_DvdCheckHook2);

        return 0;
    }

    void __stdcall UninstallAll()
    {
        // 重置完成标记
        g_DvdCheckHook1.done = false;
        g_DvdCheckHook2.done = false;
    }
}
