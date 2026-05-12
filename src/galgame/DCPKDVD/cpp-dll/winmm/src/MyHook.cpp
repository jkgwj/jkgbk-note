#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // dcpkdvd.exe 基址
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

    // 光盘检测Hook （00433852）
    BYTE g_CDRegionSig[5] = { 0xE8, 0x59, 0xEB, 0xFF, 0xFF };
    BYTE g_CDPatch[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    HookEntry g_CDHook = {
        0x00033852,        // 特征码偏移 
        g_CDRegionSig, 5,      // 特征码及长度
        { 0x00033852 },    // 补丁偏移 (与特征码起始地址相同)
        { nullptr },       // 不额外校验原始指令
        { g_CDPatch },    // 补丁数据
        { 5 },             // 补丁长度 (5字节)
        false              // 未完成标记
    };


    //序列号(0044A59B)
    // 补丁: jge 44A6A4 → jmp 44A6A4 + nop
    BYTE g_RegionSig[6] = { 0x0F, 0x8D, 0x03, 0x01, 0x00, 0x00 };
    BYTE g_DVDPatch[6] = { 0xE9, 0x04, 0x01, 0x00, 0x00, 0x90 };  //懒得改名字了

    HookEntry g_DVDHook = {
        0x0004A59B,        // 特征码偏移 (0044A59B - 00400000)
        g_RegionSig, 6,      // 特征码及长度
        { 0x0004A59B },    // 补丁偏移 (与特征码起始地址相同)
        { nullptr },       // 不额外校验原始指令
        { g_DVDPatch },    // 补丁数据
        { 6 },             // 补丁长度 (6字节)
        false              // 未完成标记
    };


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 dcpkdvd.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"dcpkdvd.exe");
        if (!g_BaseAddr)
            return 1;

        // 直接检测特征码并应用Hook
        ApplyHook(g_CDHook);
        ApplyHook(g_DVDHook);


        return 0;
    }


    void __stdcall UninstallAll()
    {
        // 重置完成标记 (静态补丁无法直接恢复，如需恢复需保存原始字节)
        g_DVDHook.done = false;
    }

}