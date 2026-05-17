#include "MyHook.h"
#include <detours/detours.h>

namespace MyHook
{
    DWORD g_BaseAddr = 0;

    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        return (memcmp(addr, sig, len) == 0);
    }

    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return;
        memcpy(addr, data, len);
        VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

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
                    return;
            }
            WritePatch(hk.patchOff[i], hk.patchData[i], hk.patchLen[i]);
        }
        hk.done = true;
    }

    // 补丁1：GetDriveTypeA 始终返回 5（光盘）
    BYTE g_DVD1_Sig[6] = { 0x51, 0x8D, 0x44, 0x24, 0x00, 0x50 };
    BYTE g_DVD1_Patch[6] = { 0xB8, 0x05, 0x00, 0x00, 0x00, 0xC3 };

    HookEntry g_DVDHook_GetDrive = {
        0x0000C420,
        g_DVD1_Sig, 6,
        { 0x0000C420 },
        { nullptr },
        { g_DVD1_Patch },
        { 6 },
        false
    };

    // 补丁2：伪造卷标并设置完成标志
    BYTE g_DVD2_Sig[] = { 0x83, 0xEC, 0x18, 0x56, 0x88, 0x4C };
    BYTE g_DVD2_Patch[] = {
        0x8B, 0x86, 0x20, 0x0A, 0x00, 0x00,       // mov eax, [esi+0xA20]
        0x8D, 0x80, 0x06, 0x01, 0x00, 0x00,       // lea eax, [eax+0x106]
        0x66, 0xC7, 0x00, 0x88, 0x01,             // mov word [eax], 0x188
        0xC7, 0x02, 0x54, 0x45, 0x4E, 0x53,       // mov dword [edx], 'TENS'
        0xC7, 0x42, 0x04, 0x48, 0x49, 0x5F, 0x44, // mov dword [edx+4], 'HI_D'
        0x66, 0xC7, 0x42, 0x08, 0x56, 0x44,       // mov word [edx+8], 'VD'
        0xC6, 0x42, 0x0A, 0x00,                   // mov byte [edx+0Ah], 0
        0x33, 0xC0,                               // xor eax, eax
        0xC2, 0x04, 0x00                          // ret 4
    };

    HookEntry g_DVDHook_Volume = {
        0x0000C440,
        g_DVD2_Sig, sizeof(g_DVD2_Sig),
        { 0x0000C440 },
        { nullptr },
        { g_DVD2_Patch },
        { sizeof(g_DVD2_Patch) },
        false
    };

    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 等待 tenshi_dvd.dll 加载
        for (int i = 0; i < 50; ++i)
        {
            g_BaseAddr = (DWORD)GetModuleHandleW(L"tenshi_dvd.dll");
            if (g_BaseAddr)
                break;
            Sleep(100);
        }
        if (!g_BaseAddr)
            return 1;

        ApplyHook(g_DVDHook_GetDrive);
        ApplyHook(g_DVDHook_Volume);
        return 0;
    }

    void __stdcall UninstallAll()
    {
        g_DVDHook_GetDrive.done = false;
        g_DVDHook_Volume.done = false;
    }
}