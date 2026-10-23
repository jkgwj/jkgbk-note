#include "MyHook.h"
#include <detours/detours.h>
namespace MyHook {
    // sorechiru.exe 基址
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
    // ============== HOOK1 校验值清零补丁 ==============
    // 原始地址: 0042FEB8
    // 原始序列 8字节特征：8B 10 8B 4C 24 08 89 48
    // 补丁写入前6字节：8B 10 31 C9 90 90，FEBE(89 48 1C)保留不动
    // 偏移 = 0042FEB8 - 00400000 = 0x0002FEB8
    BYTE g_SoreHook1Sig[8] = { 0x8B,0x10,0x8B,0x4C,0x24,0x08,0x89,0x48 };
    BYTE g_SoreHook1Patch[6] = { 0x8B,0x10,0x31,0xC9,0x90,0x90 };
    HookEntry g_SoreCheckHook1 = {
        0x0002FEB8,        // 偏移：0042FEB8 - 00400000 = 0002FEB8
        g_SoreHook1Sig, 8,     // 特征码升级为8字节，校验更严格
        { 0x0002FEB8 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_SoreHook1Patch },  // 补丁数据
        { 6 },             // 补丁长度（只改前6字节）
        false              // 未完成标记
    };

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"sorechiru.exe");
        if (!g_BaseAddr)
            return 1;
        // 应用校验清零HOOK
        ApplyHook(g_SoreCheckHook1);
        return 0;
    }
    void __stdcall UninstallAll()
    {
        // 重置完成标记
        g_SoreCheckHook1.done = false;
    }
}
