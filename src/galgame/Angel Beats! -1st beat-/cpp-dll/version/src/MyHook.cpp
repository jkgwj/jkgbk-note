#include "MyHook.h"
#include <detours/detours.h>

//   注释掉 = 模式A（硬编码地址）
//   取消注释 = 模式B（SarcheckFinder 通用查找）
 #define USE_MODE_B

namespace MyHook
{

    //保存Detour原始函数指针（自动管理）
    static PVOID g_DetourOriginals[16]{};
    static int   g_DetourCount = 0;

    // SiglusEngine.exe 基址
    DWORD g_BaseAddr = 0;

    // sarcheck.dll 入口点地址 （Angel Beats! -1st beat-）
    DWORD g_AlphaRomOffset = 0x69918008;     // 模式A = 绝对地址，模式B = 未使用
    LPVOID g_TargetAddr = nullptr;

    // 入口点 70 字节特征码
    BYTE g_Signature[70] = { 
        0x83,0xEC,0x04,0x50,0x53,0xE8,0x01,0x00,0x00,0x00,0xCC,
        0x58,0x89,0xC3,0x40,0x2D,0x00,0xD0,0x27,0x00,0x2D,0x7F,
        0xCF,0x09,0x10,0x05,0x74,0xCF,0x09,0x10,0x80,0x3B,0xCC,
        0x75,0x19,0xC6,0x03,0x00,0xBB,0x00,0x10,0x00,0x00,0x68,
        0x06,0xD6,0x9D,0x28,0x68,0xCA,0xC4,0x60,0x23,0x53,0x50,
        0xE8,0x0A,0x00,0x00,0x00,0x83,0xC0,0x00,0x89,0x44,0x24,
        0x08,0x5B,0x58,0xC3
    };

    DWORD g_SigLen    = 70;
    DWORD g_PatchValue = 0x90000CC2;  // RET 0xC / NOP
    bool  g_AlphaRomPatched = false;


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
        if (hk.done || g_DetourCount >= 16)
            return;

        if (hk.sig != nullptr && !VerifySignature(hk.sigOff, hk.sig, hk.sigLen))
            return;

        // 执行时断点模式
        if (hk.patch_mod == 1)
        {
            for (int i = 0; i < 3 && hk.patchLen[i]; ++i)
            {
                if (hk.patchOff[i] == 0 || hk.patchData[i] == nullptr)
                    continue;

                LPVOID hookFunc = ::VirtualAlloc(nullptr, hk.patchLen[i], MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (!hookFunc) continue;

                memcpy(hookFunc, hk.patchData[i], hk.patchLen[i]);

                PVOID original = (PVOID)(g_BaseAddr + hk.patchOff[i]);
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());
                DetourAttach(&(PVOID&)original, hookFunc);
                DetourTransactionCommit();

                g_DetourOriginals[g_DetourCount++] = original;
            }

            hk.done = true;
            return;
        }

        // 直接写内存模式
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

    

    // 区域检测HOOK
    BYTE g_RegionSig[58] = {
        0x0F, 0x85, 0x9B, 0x00, 0x00, 0x00, 0x33, 0xC0,
        0xC7, 0x45, 0xEC, 0x07, 0x00, 0x00, 0x00, 0xC7,
        0x45, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x66, 0x89,
        0x45, 0xD8, 0x68, 0x68, 0x00, 0x9A, 0x00, 0x8D,
        0x55, 0xD8, 0x89, 0x45, 0xFC, 0x8D, 0x4D, 0xC0,
        0xE8, 0xD4, 0x44, 0xF9, 0xFF, 0x83, 0xC4, 0x04,
        0xC6, 0x45, 0xFC, 0x02, 0x83, 0x7D, 0xEC, 0x08,
        0x72, 0x0B
    };

    BYTE g_RegionPatch[6] = { 0xE9, 0x31, 0x02, 0x00, 0x00, 0x90 };

    HookEntry g_RegionHook = {
        0x0015FF4F,          // 特征码偏移
        g_RegionSig,         // 特征码指针
        58,                  // 特征码长度 ← 改为 58
        { 0x0015FF4F },      // 补丁偏移
        { nullptr },         // 原始代码（无校验）
        { g_RegionPatch },   // 补丁数据
        { 6 },               // 补丁长度
        false,               // done
        0                    // patch_mod = 0（直接写内存）
    };

    // DVD检测HOOK

    // DVD检测HOOK 
    BYTE g_DVDSig[16] = {
        0x32, 0xC0, 0xE9, 0xCC, 0x02, 0x00, 0x00, 0x33,
        0xC0, 0xC7, 0x45, 0xA0, 0x07, 0x00, 0x00, 0x00,
    };

    BYTE g_DVDPatch1[2] = { 0xB0, 0x01 };
    BYTE g_DVDPatch2[5] = { 0xE9, 0xB7, 0x01, 0x00, 0x00 };    //E9 B7 01 00 00
    BYTE g_DVDTargetOrig[2] = { 0x8A, 0xC3 };
    BYTE g_DVDTargetPatch[2] = { 0xB0, 0x01 };

    HookEntry g_DVDHook = {
        0x00153E6C,          // 特征码偏移
        g_DVDSig,            // 特征码指针
        16,                  // 特征码长度
        { 0x00153E6C, 0x00153F81, 0x0015413D },
        { nullptr, nullptr, g_DVDTargetOrig },
        { g_DVDPatch1, g_DVDPatch2, g_DVDTargetPatch },
        { 2,5,2 },           // 补丁长度
        false,
        0
    };

    // 保存原始 VirtualAlloc 函数指针
    static decltype(&::VirtualAlloc) g_OriginalVirtualAlloc = ::VirtualAlloc;

#ifdef USE_MODE_B


    // Themida 在内存中留下的 sarcheck.dll 元数据结构
    struct SarcheckInfo
    {
        LPCSTR cpDllName;  // "sarcheck.dll" 字符串指针
        PDWORD pDllData;   // DLL PE 数据指针
        DWORD  nDllSize;   // DLL 大小
    };

    static LPVOID g_AddressTable[100]{}; // 候选地址表
    static bool   g_FoundSarcheck = false; // 是否已找到

    // 将候选地址加入地址表
    static void PushAddress(LPVOID lpAddress)
    {
        for (auto& addr : g_AddressTable)
        {
            if ((addr == nullptr) || (addr == lpAddress))
            {
                addr = lpAddress;
                return;
            }
        }
    }

    // 遍历地址表，查找 SarcheckInfo 结构
    static SarcheckInfo* QuerySarcheck()
    {
        MEMORY_BASIC_INFORMATION mbi{};

        for (auto& addr : g_AddressTable)
        {
            if (addr == nullptr) return nullptr;

            if (!::VirtualQuery(addr, &mbi, sizeof(mbi))) continue;
            if (!(mbi.Type == MEM_PRIVATE &&
                  mbi.State == MEM_COMMIT &&
                  mbi.AllocationProtect == PAGE_READWRITE)) continue;

            SarcheckInfo* info = (SarcheckInfo*)addr;

            if (info->cpDllName == nullptr) continue;
            if (!::VirtualQuery(info->cpDllName, &mbi, sizeof(mbi))) continue;
            if (!(mbi.Type == MEM_IMAGE &&
                  mbi.State == MEM_COMMIT &&
                  mbi.AllocationProtect == PAGE_EXECUTE_WRITECOPY)) continue;

            if (::strncmp("sarcheck.dll", info->cpDllName, 13)) continue;

            if (!info->pDllData) continue;
            if (((PIMAGE_DOS_HEADER)info->pDllData)->e_magic != IMAGE_DOS_SIGNATURE) continue;

            return info;
        }

        return nullptr;
    }

    // DLL 数据中搜索特征码并 patch
    void __stdcall PatchAlphaRom()
    {
        SarcheckInfo* info = QuerySarcheck();
        if (!info) return;

        PBYTE base = (PBYTE)info->pDllData;
        DWORD size = info->nDllSize;

        // 在 DLL 数据范围内搜索特征码
        for (DWORD i = 0; i <= size - g_SigLen; i++)
        {
            if (::memcmp(base + i, g_Signature, g_SigLen) != 0)
                continue;

            // 找到特征码，写入补丁
            DWORD oldProtect = 0;
            if (!::VirtualProtect(base + i, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
                return;

            *(DWORD*)(base + i) = g_PatchValue;

            ::VirtualProtect(base + i, 4, oldProtect, &oldProtect);

            g_AlphaRomPatched = true;
            return;
        }
    }

#else

    void __stdcall PatchAlphaRom()
    {
        // 确认目标地址可访问
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(g_TargetAddr, &mbi, sizeof(mbi)))
            return;
        if (mbi.State != MEM_COMMIT)
            return;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return;

        //比对特征码
        if (::memcmp(g_TargetAddr, g_Signature, g_SigLen) != 0)
            return;

        //修改为可写
        DWORD oldProtect = 0;
        if (!::VirtualProtect(g_TargetAddr, 4, PAGE_EXECUTE_READWRITE, &oldProtect))
            return;

        //写入补丁
        *(DWORD*)g_TargetAddr = g_PatchValue;

        //恢复保护属性
        ::VirtualProtect(g_TargetAddr, 4, oldProtect, &oldProtect);

        //标记完成
        g_AlphaRomPatched = true;
    }
#endif // USE_MODE_B

    //拦截 VirtualAlloc
    static LPVOID WINAPI VirtualAllocHandler(
        LPVOID lpAddress,
        SIZE_T dwSize,
        DWORD  flAllocationType,
        DWORD  flProtect)
    {
        // 调用原始 VirtualAlloc
        LPVOID result = g_OriginalVirtualAlloc(lpAddress, dwSize,
                                                flAllocationType, flProtect);

#ifdef USE_MODE_B
        // SarcheckFinder
        if (!g_FoundSarcheck && result
            && (dwSize > 8)
            && (flAllocationType == MEM_COMMIT)
            && (flProtect == PAGE_READWRITE))
        {
            PushAddress(result);

            SarcheckInfo* info = QuerySarcheck();
            if (info)
            {
                PatchAlphaRom();
                g_FoundSarcheck = true;
            }
        }
#else
        //硬编码地址
        if (!g_AlphaRomPatched && result)
        {
            PatchAlphaRom();
        }
#endif

        // 通用 Hook 点 — 代码解密后自动匹配
        if (result)
        {
            ApplyHook(g_RegionHook);
            ApplyHook(g_DVDHook);
        }

        return result;
    }


    DWORD __stdcall InitHook(LPVOID lpParam)
    {
        // 获取 SiglusEngine.exe 基址
        g_BaseAddr = (DWORD)::GetModuleHandleW(L"SiglusEngine.exe");

#ifdef USE_MODE_B
        // 不需要计算 g_TargetAddr（在 DLL 数据中搜索）
#else
        // 使用绝对地址
        g_TargetAddr = (LPVOID)g_AlphaRomOffset;
#endif

        // 安装 VirtualAlloc Hook
        ::DetourRestoreAfterWith();
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourAttach(&(PVOID&)g_OriginalVirtualAlloc, VirtualAllocHandler);
        ::DetourTransactionCommit();
        return 0;
    }


    void __stdcall UninstallAll()
    {
        ::DetourTransactionBegin();
        ::DetourUpdateThread(::GetCurrentThread());
        ::DetourDetach(&(PVOID&)g_OriginalVirtualAlloc, VirtualAllocHandler);
        ::DetourTransactionCommit();
    }

}
