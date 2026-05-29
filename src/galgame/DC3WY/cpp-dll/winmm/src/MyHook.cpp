#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{
    DWORD g_BaseAddr = 0;

    // 内存特征码校验
    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(addr, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        return (::memcmp(addr, sig, len) == 0);
    }

    // 内存写入补丁
    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) return;
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    // 执行补丁
    void ApplyHook(HookEntry& hk)
    {
        if (hk.done) return;
        if (!VerifySignature(hk.sigOff, hk.sig, hk.sigLen)) return;
        for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
        {
            if (hk.patchOrig[i] && !VerifySignature(hk.patchOff[i], hk.patchOrig[i], hk.patchLen[i]))
                return;
            WritePatch(hk.patchOff[i], hk.patchData[i], hk.patchLen[i]);
        }
        hk.done = true;
    }

    // ========== 绕过DVD检测 ==========
    BYTE g_DVDSig[] = { 0x74, 0x25 };
    BYTE g_DVDPatch[] = { 0x90, 0x90 }; // NOP 取消跳转
    HookEntry g_DVDCheckHook = {
        0xE691,
        g_DVDSig, sizeof(g_DVDSig),
        { 0xE691 },
        { nullptr },
        { g_DVDPatch },
        { sizeof(g_DVDPatch) },
        false
    };

    // ========== 绕过序列号验证 ==========
    BYTE g_SerialSig[] = { 0x83,0xEC,0x18,0xA1,0xA8,0x46,0x4A,0x00 };
    BYTE g_SerialPatch[] = { 0xB8,0x00,0x00,0x00,0x00,0xC3,0x90,0x90 }; // mov eax,0 + ret + NOP补位
    HookEntry g_SerialCheckHook = {
        0x3E9C0,
        g_SerialSig, sizeof(g_SerialSig),
        { 0x3E9C0 },
        { nullptr },
        { g_SerialPatch },
        { sizeof(g_SerialPatch) },
        false
    };

    // ========== 绕过KEY验证 ==========
    BYTE g_KeySig[] = { 0x81,0xEC,0x08,0x04,0x00,0x00,0xA1,0xA8,0x46,0x4A,0x00 };
    BYTE g_KeyPatch[] = { 0xB8,0x01,0x00,0x00,0x00,0xC3,0x90,0x90,0x90,0x90,0x90 }; // mov eax,1 + ret + NOP补位
    HookEntry g_KeyCheckHook = {
        0x3EB10,
        g_KeySig, sizeof(g_KeySig),
        { 0x3EB10 },
        { nullptr },
        { g_KeyPatch },
        { sizeof(g_KeyPatch) },
        false
    };

    // 初始化钩子
    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"dc3wy.exe");
        if (!g_BaseAddr) return 1;

        ApplyHook(g_DVDCheckHook);
        ApplyHook(g_SerialCheckHook);
        ApplyHook(g_KeyCheckHook);
        return 0;
    }

    // 重置钩子状态
    void __stdcall UninstallAll()
    {
        g_DVDCheckHook.done = false;
        g_SerialCheckHook.done = false;
        g_KeyCheckHook.done = false;
    }
}