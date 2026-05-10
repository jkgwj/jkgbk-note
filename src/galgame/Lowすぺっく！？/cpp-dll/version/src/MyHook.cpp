#include "MyHook.h"
#include <detours/detours.h>

//   注释掉 = 模式A（硬编码地址）
//   取消注释 = 模式B（SarcheckFinder 通用查找）
 #define USE_MODE_B

namespace MyHook
{

    // SiglusEngine.exe 基址
    DWORD g_BaseAddr = 0;

    // sarcheck.dll 入口点地址 （Lowすぺっく！？）
    DWORD g_AlphaRomOffset = 0x052C0038;     // 模式A = 绝对地址，模式B = 未使用
    LPVOID g_TargetAddr = nullptr;

    // 入口点 70 字节特征码
    BYTE g_Signature[70] = {
        0x83, 0xEC, 0x04, 0x50, 0x53, 0xE8, 0x01, 0x00, 0x00, 0x00,
        0xCC, 0x58, 0x89, 0xC3, 0x40, 0x2D, 0x00, 0x00, 0x28, 0x00,
        0x2D, 0x7F, 0xCF, 0x09, 0x10, 0x05, 0x74, 0xCF, 0x09, 0x10,
        0x80, 0x3B, 0xCC, 0x75, 0x19, 0xC6, 0x03, 0x00, 0xBB, 0x00,
        0x10, 0x00, 0x00, 0x68, 0xFC, 0xDC, 0x7A, 0x71, 0x68, 0x1E,
        0x9F, 0x9A, 0x0E, 0x53, 0x50, 0xE8, 0x0A, 0x00, 0x00, 0x00,
        0x83, 0xC0, 0x00, 0x89, 0x44, 0x24, 0x08, 0x5B, 0x58, 0xC3
    };

    DWORD g_SigLen    = 70;
    DWORD g_PatchValue = 0x90000CC2;  // RET 0xC / NOP
    bool  g_AlphaRomPatched = false;

    //区域检测
    DWORD g_RegionCheckOffset = 0x00201DEF;
    BYTE  g_RegionCheckSig[6]   = { 0x0F, 0x85, 0x9B, 0x00, 0x00, 0x00 }; // jne 601E90
    BYTE  g_RegionCheckPatch[6] = { 0xE9, 0x31, 0x02, 0x00, 0x00, 0x90 }; // jmp 602025 + nop
    bool  g_RegionPatched = false;

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

    void __stdcall PatchRegionCheck()
    {
        // 计算区域检测实际地址
        LPVOID addr = (LPVOID)(g_BaseAddr + g_RegionCheckOffset);

        // 确认目标地址可访问
        MEMORY_BASIC_INFORMATION mbi{};
        if (!::VirtualQuery(addr, &mbi, sizeof(mbi)))
            return;
        if (mbi.State != MEM_COMMIT)
            return;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return;

        // 比对特征码（代码解密后才匹配）
        if (::memcmp(addr, g_RegionCheckSig, sizeof(g_RegionCheckSig)) != 0)
            return;

        // 修改为可写
        DWORD oldProtect = 0;
        if (!::VirtualProtect(addr, sizeof(g_RegionCheckPatch), PAGE_EXECUTE_READWRITE, &oldProtect))
            return;

        // 写入补丁
        ::memcpy(addr, g_RegionCheckPatch, sizeof(g_RegionCheckPatch));

        // 恢复保护属性
        ::VirtualProtect(addr, sizeof(g_RegionCheckPatch), oldProtect, &oldProtect);

        // 标记完成
        g_RegionPatched = true;
    }

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

        // 区域检测
        if (!g_RegionPatched && result)
        {
            PatchRegionCheck();
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
