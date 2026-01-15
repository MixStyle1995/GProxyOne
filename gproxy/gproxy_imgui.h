#ifndef GPROXY_IMGUI_H
#define GPROXY_IMGUI_H

#include "Include.h"

// Forward declarations
class CGProxy;

// D3D9 globals
extern LPDIRECT3D9              g_pD3D;
extern LPDIRECT3DDEVICE9        g_pd3dDevice;
extern D3DPRESENT_PARAMETERS    g_d3dpp;

// ImGui state
extern bool g_ImGuiInitialized;
extern bool scrollToBottom;
extern int selectedWar3Version;
extern std::string g_PopupMessage;
extern int g_PopupMessageType;
extern char serverBuffer[256];
extern char botMapBuffer[256];
extern char botAPIMapBuffer[256];

// PVPGN Game
struct PvPGNGameInfo 
{
    std::string gameName;
    std::string hostName;
    std::string mapName;
    int currentPlayers;
    int maxPlayers;
    std::string status;

    PvPGNGameInfo() : currentPlayers(0), maxPlayers(0) {}
};

// ImGui console functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
void InitImGuiConsole();
void ShutdownImGuiConsole();
void RenderImGuiConsole();
void GuiThread();
bool IsAdminUser(const std::string& szuser);

// Color conversion
ImVec4 GetColorFromPair(int color_pair);

// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

inline std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();
        
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], size_needed, NULL, NULL);
    return result;
}

#endif // GPROXY_IMGUI_H
