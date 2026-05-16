#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // dc5sh.exe 基址
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

    // ============== HOOK1 过软电池（核心校验返回值修改） ==============
    BYTE g_Hook1Sig[6] = { 0x8A,0xC3,0xEB,0x02,0x32,0xC0 };
    BYTE g_Hook1Patch[6] = { 0xB0,0x01,0xEB,0x02,0xB0,0x01 };

    HookEntry g_SoftBatteryHook1 = {
        0x0000149A,        // 偏移：0040149A - 00400000 = 0000149A
        g_Hook1Sig, 6,     // 特征码及长度
        { 0x0000149A },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_Hook1Patch },  // 补丁数据
        { 6 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== HOOK2 过软电池（检查是否安装软电池） ==============
    BYTE g_Hook2Sig[5] = { 0xE8,0x39,0xF4,0xFF,0xFF };
    BYTE g_Hook2Patch[19] = {
        0x90,0x90,0x90,0x90,0x90,          // 5个NOP 覆盖原CALL
        0x8B,0x7C,0x24,0x0C,                // 保留原指令
        0xE9,0x20,0x01,0x00,0x00,          // JMP 4021B0
        0x90,0x90,0x90,                    // 3个NOP
        0x83,0xC4                     // 保留原指令
    };

    HookEntry g_SoftBatteryHook2 = {
        0x00002082,        // 偏移：00402082 - 00400000 = 00002082
        g_Hook2Sig, 5,     // 特征码及长度
        { 0x00002082 },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_Hook2Patch },  // 补丁数据
        { 19 },            // 补丁长度
        false              // 未完成标记
    };


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 dc5sh.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"dc5sh.exe");
        if (!g_BaseAddr)
            return 1;

        // 应用两个软电池HOOK
        ApplyHook(g_SoftBatteryHook1);
        ApplyHook(g_SoftBatteryHook2);

        return 0;
    }


    void __stdcall UninstallAll()
    {
        // 重置完成标记
        g_SoftBatteryHook1.done = false;
        g_SoftBatteryHook2.done = false;
    }

}