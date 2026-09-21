#include "MyHook.h"
#include <detours/detours.h>
#include <string.h>

namespace MyHook {
    static DWORD g_BaseAddr = 0;

    // ========== Hook 1: 函数2硬编码 (RVA 0x18A00, VA 0x418A00) ==========
    BYTE g_Func2Sig[18] = {
        0x8B,0x15,0x30,0xF1,0xB9,0x00,0x8B,0x4C,
        0x24,0x04,0xE8,0x11,0x9F,0xFF,0xFF,0x33,
        0xC0,0xC3
    };
    BYTE g_Func2Orig[6] = { 0x8B,0x15,0x30,0xF1,0xB9,0x00 };
    BYTE g_Func2Patch[6] = { 0xBA,0xE2,0x9F,0xBD,0xAC,0x90 };

    HookEntry g_Func2Hook = {
        0x00018A00, g_Func2Sig, 18,
        { 0x00018A00, 0, 0 },
        { g_Func2Orig, nullptr, nullptr },
        { g_Func2Patch, nullptr, nullptr },
        { 6, 0, 0 }, false, false
    };

    // ========== Hook 2: DVD/盘符检测 (RVA 0x1B9CE, VA 0x41B9CE) ==========
    BYTE g_DriveSig[12] = {
        0xE8,0x7D,0xCA,0xFE,0xFF,0x83,0xC4,0x04,0x8B,0xCE,0x8A,0xD8
    };
    BYTE g_DriveOrig[5] = { 0xE8,0x7D,0xCA,0xFE,0xFF };
    BYTE g_DrivePatch[5] = { 0xB8,0xA4,0x73,0x31,0x73 };

    HookEntry g_DriveHook = {
        0x0001B9CE, g_DriveSig, 12,
        { 0x0001B9CE, 0, 0 },
        { g_DriveOrig, nullptr, nullptr },
        { g_DrivePatch, nullptr, nullptr },
        { 5, 0, 0 }, false, false
    };

    // ========== Hook 3: 文件搜索跳转 (RVA 0x1FE57, VA 0x41FE57) ==========
    // 原: E8 14 4C FE FF   call 404A70
    // 改: E9 ?? ?? ?? ??   jmp  CaveEntry
    BYTE g_FileSig[16] = {
        0x6A,0x00,0x68,0x00,0x00,0x00,0x80,0x50,
        0xE8,0x14,0x4C,0xFE,0xFF,0x83,0xC4,0x18
    };
    BYTE g_FileOrig[5] = { 0xE8,0x14,0x4C,0xFE,0xFF };

    HookEntry g_FileHook = {
        0x0001FE4F, g_FileSig, 16,
        { 0x0001FE57, 0, 0 },
        { g_FileOrig, nullptr, nullptr },
        { nullptr, nullptr, nullptr },
        { 5, 0, 0 }, false, false
    };

    // ========== Cave (naked 内联汇编, 编译器自动算 rel32) ==========
    //
    // 调用点 0x41FE57 原来是 call, 现在改成 jmp 到这里。
    // jmp 不压返回地址, 所以栈布局:
    //   [esp+00] = 第6参 (push eax)
    //   [esp+04] = 第5参 (80000000)
    //   [esp+08] = 第4参 (0)
    //   [esp+0C] = 第3参 (edx = [ebp-34] 指针)  <-- 写 sentinel 的目标
    //   [esp+10] = 第2参
    //   [esp+14] = 第1参
    //
    // eax 在进入时仍为调用前的值 (jmp 不改寄存器), [eax+A] 指向文件名。

    extern "C" __declspec(naked) void __cdecl CaveEntry()
    {
        __asm
        {
            cmp  dword ptr[eax + 0Ah], 6E69622Eh    // ".bin"
            jne  CallOriginal

            // .bin: 写 sentinel, 返回0, 跳回 add esp,18
            mov  eax, [esp + 0Ch]
            mov  dword ptr[eax], 7B346447h
            xor eax, eax
            mov  ecx, 41FE5Ch
            jmp  ecx

            CallOriginal :
            // 非.bin: 压返回地址, 跳原函数
            push 41FE5Ch
            mov  ecx, 404A70h
            jmp  ecx
        }
    }

    // ========== 工具函数 ==========
    bool VerifySignature(DWORD offset, BYTE* sig, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(addr, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        return (::memcmp(addr, sig, len) == 0);
    }

    void WritePatch(DWORD offset, BYTE* data, DWORD len)
    {
        LPVOID addr = (LPVOID)(g_BaseAddr + offset);
        DWORD oldProtect = 0;
        ::VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
        ::memcpy(addr, data, len);
        ::VirtualProtect(addr, len, oldProtect, &oldProtect);
    }

    void ApplyHook(HookEntry& hk)
    {
        if (hk.done) return;
        if (hk.sig != nullptr && !VerifySignature(hk.sigOff, hk.sig, hk.sigLen))
            return;
        for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
        {
            if (hk.patchOff[i] == 0 || hk.patchData[i] == nullptr) continue;
            if (hk.patchOrig[i])
            {
                if (!VerifySignature(hk.patchOff[i], hk.patchOrig[i], hk.patchLen[i]))
                    return;
            }
            WritePatch(hk.patchOff[i], hk.patchData[i], hk.patchLen[i]);
        }
        hk.done = true;
    }

    // ========== 统一尝试所有 hook ==========
    void TryAllHooks()
    {
        ApplyHook(g_Func2Hook);
        ApplyHook(g_DriveHook);

        // 文件搜索: 特征码匹配后, 把 call 改成 jmp 到 CaveEntry
        if (!g_FileHook.done)
        {
            if (VerifySignature(0x1FE4F, g_FileSig, 16))
            {
                BYTE patch[5];
                patch[0] = 0xE9;
                *(DWORD*)(patch + 1) =
                    (DWORD)CaveEntry - (g_BaseAddr + 0x1FE57 + 5);

                WritePatch(0x1FE57, patch, 5);
                g_FileHook.done = true;
            }
        }
    }

    // ========== VirtualAlloc Hook ==========
    typedef LPVOID(WINAPI* VirtualAlloc_t)(LPVOID, SIZE_T, DWORD, DWORD);

    static VirtualAlloc_t g_OrigVA_Kernel32 = nullptr;
    static VirtualAlloc_t g_OrigVA_KernelBase = nullptr;

    static LPVOID WINAPI VA_Handler(
        LPVOID addr, SIZE_T size, DWORD type, DWORD protect)
    {
        LPVOID ret = nullptr;
        if (g_OrigVA_KernelBase)
            ret = g_OrigVA_KernelBase(addr, size, type, protect);
        else if (g_OrigVA_Kernel32)
            ret = g_OrigVA_Kernel32(addr, size, type, protect);

        if (ret)
            TryAllHooks();

        return ret;
    }

    // ========== 后台重试线程 ==========
    static DWORD WINAPI RetryThread(LPVOID /*lpParam*/)
    {
        Sleep(100);
        for (int i = 0; i < 500; ++i)
        {
            TryAllHooks();
            if (g_Func2Hook.done && g_DriveHook.done && g_FileHook.done)
                break;
            Sleep(200);
        }
        return 0;
    }

    DWORD __stdcall InitHook(LPVOID /*lpParam*/)
    {
        g_BaseAddr = (DWORD)::GetModuleHandleW(nullptr);
        if (g_BaseAddr == 0) return 0;

        ::DetourRestoreAfterWith();
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());

        g_OrigVA_Kernel32 = (VirtualAlloc_t)::GetProcAddress(
            ::GetModuleHandleA("kernel32.dll"), "VirtualAlloc");
        if (g_OrigVA_Kernel32)
            ::DetourAttach(&(PVOID&)g_OrigVA_Kernel32, VA_Handler);

        HMODULE hKB = ::GetModuleHandleA("kernelbase.dll");
        if (!hKB) hKB = ::LoadLibraryA("kernelbase.dll");
        if (hKB)
        {
            g_OrigVA_KernelBase = (VirtualAlloc_t)::GetProcAddress(hKB, "VirtualAlloc");
            if (g_OrigVA_KernelBase)
                ::DetourAttach(&(PVOID&)g_OrigVA_KernelBase, VA_Handler);
        }

        ::DetourTransactionCommit();
        ::CreateThread(NULL, 0, RetryThread, NULL, 0, NULL);
        return 0;
    }

    void __stdcall UninstallAll()
    {
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        if (g_OrigVA_Kernel32)
            ::DetourDetach(&(PVOID&)g_OrigVA_Kernel32, VA_Handler);
        if (g_OrigVA_KernelBase)
            ::DetourDetach(&(PVOID&)g_OrigVA_KernelBase, VA_Handler);
        ::DetourTransactionCommit();
    }
}
