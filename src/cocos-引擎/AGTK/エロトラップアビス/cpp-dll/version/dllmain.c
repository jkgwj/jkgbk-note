#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include "proxydll.h"

#define bool  int
#define true  1
#define false 0

/* 描述一个 Hook 点的配置信息 */
typedef struct HookPointConfig {
    const char* targetModule;   // 要 Hook 的目标模块名（例如 "player.exe"）
    DWORD       offset;         // 指令在模块中的相对偏移量
    DWORD       hookLen;        // 需要覆盖的指令长度（字节）
    LPVOID      hook_handler;   // 自定义的 naked 处理函数指针
    BYTE* origBytes;      // 备份原始指令用的缓冲区（大小需 ≥ hookLen）
    bool        installed;      // 标记该 Hook 是否已安装
} HookPointConfig;

#define MAX_HOOKS 10
HookPointConfig g_Hooks[MAX_HOOKS]; // 存放所有 Hook 配置的数组
int g_HookCount = 0;                // 当前已添加的 Hook 数量

/* ------ 全局工作变量 ------ */
int   g_DumpCounter = 0;           // dump 文件序号
DWORD g_TempEAX = 0;           // 临时保存 EAX 寄存器 (看情况使用)
DWORD g_TempEDX = 0;           // 临时保存 EDX 寄存器 (看情况使用)
void* g_SavedBuffer = NULL;        // 缓存中的明文数据地址
DWORD g_SavedSize = 0;           // 明文数据大小
DWORD g_RealHookAddr = 0;           // 计算后 Hook 点的真实内存地址
DWORD g_ReturnAddr = 0;           // 模拟 call 指令后应返回的游戏地址

/* ------ 函数声明 ------ */
void __stdcall DumpResource(void* data, DWORD size);          // 将指定内存数据保存为文件
const char* GetFileExtensionFromHeader(const void* data, size_t size); // 根据文件头魔数返回扩展名
void HookHandler(void);                                        // naked 函数，Hook 的核心处理逻辑
bool InstallHook(HookPointConfig* cfg);                        // 安装单个 Hook
bool UninstallHook(HookPointConfig* cfg);                      // 卸载单个 Hook
void UninstallAll(void);                                       // 卸载所有已安装的 Hook
void AddHooks(HookPointConfig* cfg);                           // 添加一个 Hook 配置到全局数组
void InitHook(void);                                           // 初始化并安装第一个 Hook（异步调用）



BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 异步初始化HOOK
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitHook, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        UninstallAll();
        if (!lpReserved)
            real_dll_free();
        break;
    default:
        break;
    }
    return TRUE;
}


void InitHook(void) {
    real_dll_init();

    // 初始化HOOK配置
    static BYTE backup[6];
    HookPointConfig cfg;
    cfg.targetModule = "player.exe";
    cfg.offset = 0x17AC44;
    cfg.hookLen = 6;
    cfg.hook_handler = HookHandler;
    cfg.origBytes = backup;
    cfg.installed = false;

    AddHooks(&cfg);
    InstallHook(&g_Hooks[0]);
}

void __stdcall DumpResource(void* data, DWORD size) {
    if (!data || size < 16) return;

    // 自动识别后缀
    const char* ext = GetFileExtensionFromHeader(data, size);

    // 生成文件名：dump_序号.后缀
    char path[MAX_PATH];
    wsprintfA(path, "out\\dump_%d%s", g_DumpCounter++, ext);

    // 自动创建输出目录
    CreateDirectoryA("out", NULL);

    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
    Sleep(100);
}

__declspec(naked) void HookHandler(void) {
    __asm {
        mov g_TempEAX, eax
        mov g_SavedBuffer, esi
        mov eax, [esp]
        mov g_SavedSize, eax
        mov eax, g_TempEAX

        push esi
        mov eax, [eax + 8]

        pushad
        pushfd

        push g_SavedSize
        push g_SavedBuffer
        call DumpResource

        popfd
        popad

        mov g_TempEDX, edx
        mov edx, g_ReturnAddr
        push edx
        mov edx, g_TempEDX
        jmp eax
    }
}

bool InstallHook(HookPointConfig* cfg) {
    if (cfg->installed) return true;

    HMODULE hMod = GetModuleHandleA(cfg->targetModule);
    if (!hMod) return false;

    g_RealHookAddr = (DWORD)hMod + cfg->offset;
    g_ReturnAddr = g_RealHookAddr + cfg->hookLen;

    if (!cfg->hook_handler) cfg->hook_handler = HookHandler;
    if (cfg->hookLen < 5) return false;
    if (!cfg->origBytes) return false;

    memcpy(cfg->origBytes, (LPCVOID)g_RealHookAddr, cfg->hookLen);

    BYTE newCode[16];
    memset(newCode, 0x90, cfg->hookLen);
    newCode[0] = 0xE9;
    *(DWORD*)(newCode + 1) = (DWORD)cfg->hook_handler - (g_RealHookAddr + 5);

    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)g_RealHookAddr, cfg->hookLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    memcpy((LPVOID)g_RealHookAddr, newCode, cfg->hookLen);
    VirtualProtect((LPVOID)g_RealHookAddr, cfg->hookLen, oldProtect, &oldProtect);

    cfg->installed = true;
    return true;
}

bool UninstallHook(HookPointConfig* cfg) {
    if (!cfg->installed) return true;
    if (!cfg->origBytes) return false;

    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)g_RealHookAddr, cfg->hookLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    memcpy((LPVOID)g_RealHookAddr, cfg->origBytes, cfg->hookLen);
    VirtualProtect((LPVOID)g_RealHookAddr, cfg->hookLen, oldProtect, &oldProtect);

    cfg->installed = false;
    return true;
}

void UninstallAll(void) {
    for (int i = 0; i < g_HookCount; ++i)
        UninstallHook(&g_Hooks[i]);
}

void AddHooks(HookPointConfig* cfg) {
    if (g_HookCount < MAX_HOOKS) {
        g_Hooks[g_HookCount] = *cfg;
        g_HookCount++;
    }
}
const char* GetFileExtensionFromHeader(const void* data, size_t size)
{
    if (!data || size < 4) return ""; // 数据太小无法识别
    const unsigned char* buf = (const unsigned char*)data;

    // ===================== 图片格式 =====================
    if (size >= 8 && buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47)
        return ".png";
    if (size >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
        return ".jpg";
    if (size >= 6 && buf[0] == 0x47 && buf[1] == 0x49 && buf[2] == 0x46 && buf[3] == 0x38)
        return ".gif";
    if (size >= 2 && buf[0] == 0x42 && buf[1] == 0x4D)
        return ".bmp";
    if (size >= 4 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 && buf[3] == 0x00)
        return ".ico";
    if (size >= 4 && buf[0] == 0x52 && buf[1] == 0x49 && buf[2] == 0x46 && buf[3] == 0x46)
        return ".webp";
    if (size >= 4 && buf[0] == 0x49 && buf[1] == 0x49 && buf[2] == 0x2A && buf[3] == 0x00)
        return ".tiff";
    if (size >= 4 && buf[0] == 0x4D && buf[1] == 0x4D && buf[2] == 0x00 && buf[3] == 0x2A)
        return ".tiff";

    // ===================== 音频格式 =====================
    if (size >= 4 && buf[0] == 0x52 && buf[1] == 0x49 && buf[2] == 0x46 && buf[3] == 0x46)
        return ".wav";
    if (size >= 3 && buf[0] == 0x49 && buf[1] == 0x44 && buf[2] == 0x33)
        return ".mp3";
    if (size >= 4 && buf[0] == 0xFF && buf[1] == 0xFB)
        return ".mp3";
    if (size >= 4 && buf[0] == 0x4F && buf[1] == 0x67 && buf[2] == 0x67 && buf[3] == 0x53)
        return ".ogg";
    if (size >= 12 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x1C)
        return ".mp4";
    if (size >= 4 && buf[0] == 0x66 && buf[1] == 0x74 && buf[2] == 0x79 && buf[3] == 0x70)
        return ".mp4";
    if (size >= 4 && buf[0] == 0x46 && buf[1] == 0x4C && buf[2] == 0x41 && buf[3] == 0x43)
        return ".flac";
    if (size >= 4 && buf[0] == 0x4D && buf[1] == 0x54 && buf[2] == 0x68 && buf[3] == 0x64)
        return ".midi";

    // ===================== 文本/配置格式 =====================
    if (size >= 2 && (buf[0] == '{' || buf[0] == '['))
    {
        BOOL hasColon = FALSE;
        BOOL hasQuote = FALSE;
        size_t limit = size < 512 ? size : 512;   // 只扫描前 512 字节，避免过大文件开销
        for (size_t i = 1; i < limit; ++i)
        {
            if (buf[i] == ':') hasColon = TRUE;
            if (buf[i] == '"') hasQuote = TRUE;
            // 如果发现冒号，基本可以确定为 JSON 对象；
            // 如果是以数组开头，通常会有引号或逗号，这里简单用引号判断也很可靠
            if (hasColon || (buf[0] == '[' && hasQuote))
                return ".json";
        }
        // 如果没找到冒号或引号，但以 { 或 [ 开头，也极大概率是 JSON，可以直接返回
        return ".json";
    }
    if (size >= 5 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        return ".txt"; // UTF-8 BOM文本
    if (size >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)
        return ".txt"; // UTF-16 LE文本
    if (size >= 2 && buf[0] == 0xFE && buf[1] == 0xFF)
        return ".txt"; // UTF-16 BE文本
    // 纯ASCII文本检测
    {
        int is_text = 1;
        for (size_t i = 0; i < min(size, 256); i++) {
            if (buf[i] < 0x20 && buf[i] != 0x09 && buf[i] != 0x0A && buf[i] != 0x0D) {
                is_text = 0;
                break;
            }
        }
        if (is_text) return ".txt";
    }
    if (size >= 5 && memcmp(buf, "<?xml", 5) == 0)
        return ".xml";
    if (size >= 4 && memcmp(buf, "<!--", 4) == 0)
        return ".xml";
    if (size >= 7 && memcmp(buf, "<!DOCTYPE", 9) == 0)
        return ".html";
    if (size >= 6 && memcmp(buf, "<html>", 6) == 0)
        return ".html";

    // ===================== 压缩包/归档格式 =====================
    if (size >= 4 && buf[0] == 0x50 && buf[1] == 0x4B && buf[2] == 0x03 && buf[3] == 0x04)
        return ".zip";
    if (size >= 4 && buf[0] == 0x52 && buf[1] == 0x61 && buf[2] == 0x72 && buf[3] == 0x21)
        return ".rar";
    if (size >= 6 && buf[0] == 0x37 && buf[1] == 0x7A && buf[2] == 0xBC && buf[3] == 0xAF && buf[4] == 0x27 && buf[5] == 0x1C)
        return ".7z";
    if (size >= 4 && buf[0] == 0x1F && buf[1] == 0x8B && buf[2] == 0x08)
        return ".gz";
    if (size >= 4 && buf[0] == 0xFD && buf[1] == 0x37 && buf[2] == 0x7A && buf[3] == 0x58 && buf[4] == 0x5A && buf[5] == 0x00)
        return ".xz";

    // ===================== 其他常见格式 =====================
    if (size >= 4 && buf[0] == 0x25 && buf[1] == 0x50 && buf[2] == 0x44 && buf[3] == 0x46)
        return ".pdf";
    if (size >= 8 && buf[0] == 0xD0 && buf[1] == 0xCF && buf[2] == 0x11 && buf[3] == 0xE0 && buf[4] == 0xA1 && buf[5] == 0xB1 && buf[6] == 0x1A && buf[7] == 0xE1)
        return ".doc"; // 旧版Office文档

    // 无匹配，返回空后缀
    return "";
}