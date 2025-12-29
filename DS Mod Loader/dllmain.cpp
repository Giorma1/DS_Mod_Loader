// Dark Souls Remastered / Elden Ring - Single-file dinput8.dll Mod Loader
// x64 ONLY
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>
#pragma comment(lib, "user32.lib")

// ------------------------------------------------------------
// Globals
// ------------------------------------------------------------
HMODULE g_hModule;
DWORD g_loadDelayMs = 1000;

// Real dinput8
HMODULE g_realDinput8 = nullptr;
using DirectInput8Create_t = HRESULT(WINAPI*)(
    HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN
    );
DirectInput8Create_t g_DirectInput8Create = nullptr;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
std::wstring ToLower(const std::wstring& str)
{
    std::wstring out = str;
    std::transform(out.begin(), out.end(), out.begin(), towlower);
    return out;
}

// ------------------------------------------------------------
// Generate default config if missing
// ------------------------------------------------------------
void GenerateConfigIfMissing(const std::filesystem::path& cfgPath)
{
    if (!std::filesystem::exists(cfgPath))
    {
        std::ofstream cfg(cfgPath);
        cfg << "[Loader]\n";
        cfg << "LoadDelayMs=1000\n\n";
        cfg << "[Mods]\n";
        cfg << "; Lower numbers load first, higher numbers load later\n";
        cfg << "; Default priority is 100\n";
        cfg.close();
    }
}

// ------------------------------------------------------------
// Load configuration
// ------------------------------------------------------------
void LoadConfig()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);

    std::filesystem::path basePath = std::filesystem::path(path).parent_path();
    std::filesystem::path cfg = basePath / L"DS Mod Loader.ini";

    GenerateConfigIfMissing(cfg);
    g_loadDelayMs = GetPrivateProfileIntW(L"Loader", L"LoadDelayMs", 1000, cfg.c_str());
}

// ------------------------------------------------------------
// Create required folder for DLLs
// ------------------------------------------------------------
void CreateModFolders()
{
    std::filesystem::path dllFolder = L"mods\\dll";
    if (!std::filesystem::exists(dllFolder))
        std::filesystem::create_directories(dllFolder);
}

// ------------------------------------------------------------
// Update INI automatically if new DLLs are added
// ------------------------------------------------------------
void UpdateModsIni(const std::filesystem::path& cfgPath, const std::filesystem::path& dllDir)
{
    std::map<std::wstring, int> existingMods;
    std::wifstream ini(cfgPath);
    std::wstring line;
    bool inModsSection = false;

    while (std::getline(ini, line))
    {
        line.erase(0, line.find_first_not_of(L" \t\r\n"));
        line.erase(line.find_last_not_of(L" \t\r\n") + 1);
        if (line.empty() || line[0] == L';') continue;

        if (line.front() == L'[' && line.back() == L']')
        {
            inModsSection = (line == L"[Mods]");
            continue;
        }

        if (!inModsSection) continue;

        auto pos = line.find(L'=');
        if (pos != std::wstring::npos)
        {
            std::wstring key = ToLower(line.substr(0, pos));
            std::wstring val = line.substr(pos + 1);
            existingMods[key] = _wtoi(val.c_str());
        }
    }
    ini.close();

    for (auto& e : std::filesystem::directory_iterator(dllDir))
    {
        if (!e.is_regular_file() || e.path().extension() != L".dll")
            continue;
        std::wstring fileName = ToLower(e.path().filename().wstring());
        if (existingMods.find(fileName) == existingMods.end())
            existingMods[fileName] = 100;
    }

    std::wofstream out(cfgPath);
    out << "[Loader]\n";
    out << "LoadDelayMs=" << g_loadDelayMs << "\n\n";
    out << "[Mods]\n";
    for (auto& [name, priority] : existingMods)
        out << name << "=" << priority << "\n";
}

// ------------------------------------------------------------
// Load all DLL mods with priority
// ------------------------------------------------------------
void LoadDllMods()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hModule, path, MAX_PATH);
    std::filesystem::path basePath = std::filesystem::path(path).parent_path();
    std::filesystem::path dir = basePath / L"mods\\dll";
    std::filesystem::path cfg = basePath / L"DS Mod Loader.ini";

    if (!std::filesystem::exists(dir))
        return;

    UpdateModsIni(cfg, dir);

    std::map<std::wstring, int> modPriority;
    wchar_t modName[256], modValue[64];
    for (int i = 0; i < 256; i++)
    {
        if (GetPrivateProfileStringW(L"Mods", nullptr, L"", modName, sizeof(modName) / sizeof(wchar_t), cfg.c_str()) == 0)
            break;
        if (GetPrivateProfileStringW(L"Mods", modName, L"100", modValue, sizeof(modValue) / sizeof(wchar_t), cfg.c_str()) > 0)
            modPriority[ToLower(modName)] = _wtoi(modValue);
    }

    std::vector<std::pair<int, std::wstring>> modsToLoad;
    for (auto& e : std::filesystem::directory_iterator(dir))
    {
        if (!e.is_regular_file() || e.path().extension() != L".dll")
            continue;
        std::wstring fileName = ToLower(e.path().filename().wstring());
        int priority = 100;
        if (modPriority.find(fileName) != modPriority.end())
            priority = modPriority[fileName];
        modsToLoad.emplace_back(priority, e.path().wstring());
    }

    std::sort(modsToLoad.begin(), modsToLoad.end(), [](auto& a, auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
        });

    for (auto& [priority, mod] : modsToLoad)
        LoadLibraryW(mod.c_str());
}

// ------------------------------------------------------------
// Load real dinput8.dll from system
// ------------------------------------------------------------
void LoadRealDinput8()
{
    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    std::wstring realPath = std::wstring(sysPath) + L"\\dinput8.dll";
    g_realDinput8 = LoadLibraryW(realPath.c_str());
    if (g_realDinput8)
        g_DirectInput8Create = (DirectInput8Create_t)GetProcAddress(g_realDinput8, "DirectInput8Create");
}

// ------------------------------------------------------------
// Main loader thread
// ------------------------------------------------------------
DWORD WINAPI MainThread(LPVOID)
{
    LoadConfig();
    CreateModFolders();
    Sleep(g_loadDelayMs);
    LoadDllMods();
    return 0;
}

// ------------------------------------------------------------
// Export DirectInput8Create
// ------------------------------------------------------------
extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion,
    REFIID riid, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    if (!g_DirectInput8Create) return E_FAIL;
    return g_DirectInput8Create(hinst, dwVersion, riid, ppvOut, punkOuter);
}

// ------------------------------------------------------------
// DllMain
// ------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hinst;
        LoadRealDinput8();
        DisableThreadLibraryCalls(hinst);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
