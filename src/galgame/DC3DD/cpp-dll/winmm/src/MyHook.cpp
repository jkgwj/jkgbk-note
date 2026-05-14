#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{

    // DC3DD.exe 基址
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

    // ──────────── Hook 1: DVD 检测 ────────────
    // 原始: 0x00410361  je 0x410388  (74 25)
    // 修改: nop nop (90 90)
    BYTE g_DVDSig[13] = {
    0x74, 0x25,
    0x66, 0x8B, 0x0D, 0x68, 0x4F, 0x48, 0x00,
    0x8A, 0x15, 0x6A, 0x4F
    };
    // 补丁仍只改前两字节为 nop
    BYTE g_DVDPatch[6] = { 0x90, 0x90,0x66, 0x8B, 0x0D, 0x68, };

    HookEntry g_DVDHook = {
        0x00010361,          // 补丁偏移
        g_DVDSig, 13,        // 特征码长度13
        { 0x00010361 },
        { nullptr },
        { g_DVDPatch },
        { 6 },
        false
    };

    // ──────────── Hook 2: 序列号验证函数 ────────────
    // 原始开头: 0x00449260  sub esp,18; mov eax,[4B18E4]
    // 修改: mov eax,0; ret; nop; nop (B8 00 00 00 00 C3 90 90)
    BYTE g_SerialSig[8] = { 0x83, 0xEC, 0x18, 0xA1, 0xE4, 0x18, 0x4B, 0x00 };
    BYTE g_SerialPatch[8] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x90, 0x90 };

    HookEntry g_SerialHook = {
        0x00049260,        // 特征码/补丁偏移 (0x449260 - 0x400000)
        g_SerialSig, 8,    // 特征码及长度
        { 0x00049260 },    // 补丁偏移
        { nullptr },
        { g_SerialPatch },
        { 8 },
        false
    };

    // ──────────── Hook 3: 跳过 Key 验证 ────────────
    // 原始: 0x0040FF95  je 0x40FFBE  (74 27)
    // 修改: nop nop (90 90)
    BYTE g_KeySig[2] = { 0x74, 0x27 };
    BYTE g_KeyPatch[2] = { 0x90, 0x90 };

    HookEntry g_KeyHook = {
        0x0000FF95,        // 特征码/补丁偏移 (0x40FF95 - 0x400000)
        g_KeySig, 2,
        { 0x0000FF95 },
        { nullptr },
        { g_KeyPatch },
        { 2 },
        false
    };

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 DC3DD.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"DC3DD.exe");
        if (!g_BaseAddr)
            return 1;

        // 直接检测特征码并应用 Hook
        ApplyHook(g_DVDHook);
        ApplyHook(g_SerialHook);
        ApplyHook(g_KeyHook);

        return 0;
    }

    void __stdcall UninstallAll()
    {
        // 重置完成标记 (静态补丁无法直接恢复，如需恢复需保存原始字节)
        g_DVDHook.done = false;
        g_SerialHook.done = false;
        g_KeyHook.done = false;
    }

}