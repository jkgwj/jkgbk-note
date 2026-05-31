#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // futakoi.exe 基址
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

    // ============== HOOK1  ==============
    BYTE g_Hook1Sig[6] = { 0x75, 0x04, 0xB0, 0x01, 0x5E, 0xC3 };
    BYTE g_Hook1Patch[2] = { 0x90,0x90 };

    HookEntry g_SoftBatteryHook1 = {
        0x000017FF,        // 偏移：17FF
        g_Hook1Sig, 6,     // 特征码及长度
        { 0x000017FF },    // 补丁偏移
        { nullptr },       // 不额外校验原始指令
        { g_Hook1Patch },  // 补丁数据
        { 2 },             // 补丁长度
        false              // 未完成标记
    };

    // ============== HOOK2 过软电池（检查是否安装软电池） ==============
    BYTE g_Hook2Sig[5] = { 0xE8,0x39,0xF4,0xFF,0xFF };
    BYTE g_Hook2Patch[19] = {
        0xE9,0x29,0x01,0x00,0x00,          // JMP 4021B0 
        0x90,0x90,0x90,0x90,0x90,          // 后续NOP填充
        0x90,0x90,0x90,0x90,0x90,
        0x90,0x90,0x90,0x90
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
        // 获取基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"futakoi.exe");
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