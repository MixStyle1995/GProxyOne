#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

#include "gproxy.h"
#include "util.h"
#include "socket.h"
#include "commandpacket.h"
#include "bnetprotocol.h"
#include "bnet.h"
#include "gameprotocol.h"
#include "gpsprotocol.h"
#include "gproxy_imgui.h"
#include "bncsutilinterface.h"

#include <d3d9.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx9.h>
#include <tchar.h>
#include <thread>
#include <mutex>
#include <windows.h>
#include <sstream>

#pragma comment(lib, "d3d9.lib")

// Globals
LPDIRECT3D9              g_pD3D = NULL;
LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
D3DPRESENT_PARAMETERS    g_d3dpp = {};
bool                     g_ImGuiInitialized = false;

extern std::vector<ColoredLine> gMainBuffer;
extern std::string gInputBuffer;
extern std::string gChannelName;
extern std::vector<std::string> gChannelUsers;
extern CGProxy* gGProxy;
extern bool gCurses;
extern CConfig CFG;
extern bool gRestart;

static char inputBuffer[512] = "";
static char inputBuffer2[512] = "";
bool scrollToBottom = true;
static bool showConsoleWindow = false;
static HWND g_ConsoleWindow = NULL;

// Input buffers for connection settings
static char serverBuffer[256] = "";
static char usernameBuffer[256] = "";
static char passwordBuffer[256] = "";
static char botNameBuffer[256] = "";  // Bot name for host command
static char botMapBuffer[256] = "";
static int portBuffer = 6112;
int selectedWar3Version = 0; // 0=1.24, 1=1.26, 2=1.27, 3=1.28, 4=1.29, 5=1.31
static bool settingsInitialized = false;
static bool showPassword = false;  // Toggle for password visibility

// BETTER COLORS - NO GRAY!
ImVec4 GetColorFromPair(int color_pair)
{
    switch (color_pair)
    {
    case dye_black:         return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Lighter, not too dark
    case dye_blue:          return ImVec4(0.5f, 0.7f, 1.0f, 1.0f);
    case dye_green:         return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    case dye_aqua:          return ImVec4(0.3f, 1.0f, 1.0f, 1.0f);
    case dye_red:           return ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
    case dye_purple:        return ImVec4(1.0f, 0.5f, 1.0f, 1.0f);
    case dye_yellow:        return ImVec4(1.0f, 1.0f, 0.5f, 1.0f);
    case dye_white:         return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // Pure white
    case dye_grey:          return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);  // BRIGHT, not gray!
    case dye_light_blue:    return ImVec4(0.6f, 0.9f, 1.0f, 1.0f);
    case dye_light_green:   return ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
    case dye_light_aqua:    return ImVec4(0.5f, 1.0f, 1.0f, 1.0f);
    case dye_light_red:     return ImVec4(1.0f, 0.7f, 0.7f, 1.0f);
    case dye_light_purple:  return ImVec4(1.0f, 0.7f, 1.0f, 1.0f);
    case dye_light_yellow:  return ImVec4(1.0f, 1.0f, 0.7f, 1.0f);
    case dye_bright_white:  return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    default:                return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);  // Almost white
    }
}

bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = NULL; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        return;
    ImGui_ImplDX9_CreateDeviceObjects();
}

void SetupProfessionalStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;

    style.WindowPadding = ImVec2(10, 10);     // Giảm từ 12
    style.FramePadding = ImVec2(8, 5);        // Giảm từ 10, 6
    style.ItemSpacing = ImVec2(8, 6);         // Giảm từ 10, 8
    style.ItemInnerSpacing = ImVec2(6, 4);    // Giảm từ 8, 6

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    // Professional dark theme
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);  // Bright text
    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.13f, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.08f, 0.11f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.35f, 0.45f, 0.70f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.34f, 0.42f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.20f, 0.30f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.50f, 0.70f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.25f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.40f, 0.60f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.50f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.32f, 0.50f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.40f, 0.60f, 0.90f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.50f, 0.75f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.35f, 0.45f, 0.60f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.08f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.34f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.42f, 0.55f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.52f, 0.68f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.35f, 0.50f, 0.75f, 0.60f);
}

void InitImGuiConsole()
{
    if (g_ImGuiInitialized)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    SetupProfessionalStyle();

    g_ImGuiInitialized = true;
}

void ShutdownImGuiConsole()
{
    if (!g_ImGuiInitialized)
        return;

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_ImGuiInitialized = false;
}

// Global variables cho game list
std::vector<PvPGNGameInfo> gParsedGames;
std::string gGamesRawOutput;
bool gWaitingForGames = false;
uint32_t gGamesStartTime = 0;

// Helper functions
std::string TrimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

PvPGNGameInfo ParseGameLine(const std::string& line)
{
    PvPGNGameInfo info;

    try
    {
        size_t startPos = line.find_first_not_of("0123456789. \t");
        if (startPos == std::string::npos) return info;

        size_t openParen = line.find('(', startPos);
        if (openParen == std::string::npos) return info;

        info.gameName = TrimString(line.substr(startPos, openParen - startPos));

        size_t closeParen = line.find(')', openParen);
        if (closeParen == std::string::npos) return info;

        info.hostName = TrimString(line.substr(openParen + 1, closeParen - openParen - 1));

        std::string remaining = line.substr(closeParen + 1);

        size_t slotPos = remaining.find_first_of("0123456789");
        if (slotPos == std::string::npos) return info;

        info.mapName = TrimString(remaining.substr(0, slotPos));

        size_t slashPos = remaining.find('/', slotPos);
        if (slashPos != std::string::npos) {
            std::string currentStr = TrimString(remaining.substr(slotPos, slashPos - slotPos));
            info.currentPlayers = std::stoi(currentStr);

            size_t spaceAfter = remaining.find_first_of(" \t", slashPos + 1);

            if (spaceAfter != std::string::npos) {
                std::string maxStr = remaining.substr(slashPos + 1, spaceAfter - slashPos - 1);
                info.maxPlayers = std::stoi(TrimString(maxStr));
                info.status = TrimString(remaining.substr(spaceAfter + 1));
            }
            else {
                std::string maxStr = TrimString(remaining.substr(slashPos + 1));
                info.maxPlayers = std::stoi(maxStr);
                info.status = "Waiting";
            }
        }

    }
    catch (...)
    {
        return PvPGNGameInfo();
    }

    return info;
}

void ParseGamesOutput(const std::string& output)
{
    gParsedGames.clear();

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line))
    {
        if (line.empty() ||
            line.find("Games list") != std::string::npos ||
            line.find("games list") != std::string::npos ||
            line.find("---") != std::string::npos ||
            line.find("===") != std::string::npos ||
            line.find("Total:") != std::string::npos ||
            line.find("total:") != std::string::npos) {
            continue;
        }

        PvPGNGameInfo gameInfo = ParseGameLine(line);
        if (!gameInfo.gameName.empty())
        {
            gParsedGames.push_back(gameInfo);
        }
    }

    gWaitingForGames = false;
}

void CreateUpdaterBat()
{
    std::ofstream bat("update.bat", std::ios::out | std::ios::trunc);
    if (!bat.is_open())
        return;

    bat <<
        R"(@echo off
setlocal

REM ===== CONFIG =====
set APP=gproxy.exe
set URL=http://160.187.146.137/gproxy.exe
set NEW=%APP%.new
set DIR=%~dp0
set BAT=%~f0

echo Downloading update...

REM ===== DOWNLOAD FIRST =====
powershell -Command "try { (New-Object Net.WebClient).DownloadFile('%URL%', '%DIR%%NEW%'); exit 0 } catch { exit 1 }"

REM ===== CHECK DOWNLOAD =====
if not exist "%DIR%%NEW%" (
    echo Download failed. Keep old file.
    pause
    exit /b 1
)

REM ===== REPLACE EXE =====
del /f /q "%DIR%%APP%" >nul 2>&1
ren "%DIR%%NEW%" "%APP%"

echo Update success!
start "" "%DIR%%APP%"

REM ===== SELF DELETE BAT =====
cmd /c del /f /q "%BAT%"

exit
)";

    bat.close();
}

enum t_game_status
{
    game_status_started,
    game_status_full,
    game_status_open,
    game_status_loaded,
    game_status_done
};

string GetGameStatusString(t_game_status status)
{
    switch (status)
    {
        case game_status_started:   return u8"Đang chơi";    // game_status_started
        case game_status_full:      return u8"Đầy";          // game_status_full
        case game_status_open:      return u8"Chờ";          // game_status_open
        case game_status_loaded:    return u8"Đang tải";     // game_status_loaded
        case game_status_done:      return u8"Kết thúc";     // game_status_done
        default:                    return u8"Không rõ";     // Unknown
    }
}

void TextCentered(const char* text)
{
    float windowWidth = ImGui::GetContentRegionAvail().x;
    float textWidth = ImGui::CalcTextSize(text).x;

    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f + ImGui::GetCursorPosX());
    ImGui::Text("%s", text);
}

void RenderImGuiConsole()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##MainWindow", nullptr, flags);

    // Title bar
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "GProxy++ 3.2 New By Thái Sơn");

    ImGui::SameLine(ImGui::GetWindowWidth() - 320);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    if (ImGui::Button("Update Gproxy", ImVec2(140, 0)))
    {
        CreateUpdaterBat();
        if (gGProxy)
        {
            gGProxy->m_Quit = true;
            ShellExecuteA(NULL, "open", "update.bat", NULL, NULL, SW_SHOW);
        }
    }
    ImGui::PopStyleColor(3);

    /*ImGui::SameLine();
    if (ImGui::Button(showConsoleWindow ? "Hide CMD" : "Show CMD", ImVec2(140, 0)))
    {
        showConsoleWindow = !showConsoleWindow;
        if (g_ConsoleWindow)
            ShowWindow(g_ConsoleWindow, showConsoleWindow ? SW_SHOW : SW_HIDE);
    }*/

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("Exit", ImVec2(140, 0)))
    {
        if (gGProxy)
            gGProxy->m_Quit = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();

    // Tabs
    if (ImGui::BeginTabBar("##MainTabs"))
    {
        // Console tab with Channel sidebar and Settings
        if (ImGui::BeginTabItem("Console"))
        {
            // Split: Console (80%) | Channel (20%)
            ImGui::BeginChild("##ConsoleArea", ImVec2(ImGui::GetContentRegionAvail().x * 0.80f, 0), false, ImGuiWindowFlags_NoScrollbar);

            // Console output (chỉ console mới có scrollbar)
            ImGui::BeginChild("##ConsoleOutput", ImVec2(0, -280), true);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));

            for (const auto& line : gMainBuffer)
            {
                if (line.segments.empty())
                    continue;

                for (const auto& segment : line.segments)
                {
                    if (segment.text.empty())
                        continue;

                    ImVec4 color = GetColorFromPair(segment.color_pair);
                    ImGui::PushStyleColor(ImGuiCol_Text, color);

                    std::string utf8 = WideToUtf8(segment.text);
                    ImGui::TextUnformatted(utf8.c_str());
                    ImGui::SameLine(0, 0);

                    ImGui::PopStyleColor();
                }
                ImGui::NewLine();
            }

            ImGui::PopStyleVar();

            ImGui::Dummy(ImVec2(0, 1));

            if (scrollToBottom)
            {
                ImGui::SetScrollHereY(1.0f);
                scrollToBottom = false;
            }

            ImGui::EndChild();

            // Input
            ImGui::SetNextItemWidth(-100);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.3f, 0.4f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.3f, 0.4f, 0.5f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.35f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.6f, 0.8f, 1.0f));
            bool enterPressed = ImGui::InputText("##Input", inputBuffer, IM_ARRAYSIZE(inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor(4);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.4f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.25f, 0.7f, 1.0f));
            bool sendClicked = ImGui::Button("Send", ImVec2(90, 0));
            if (enterPressed || sendClicked)
            {
                if (inputBuffer[0] != '\0')
                {
                    gInputBuffer = std::string(inputBuffer);
                    inputBuffer[0] = '\0';
                    scrollToBottom = true;
                }
            }
            ImGui::PopStyleColor(3);

            ImGui::SetItemDefaultFocus();

            ImGui::Separator();

            ImGui::SetNextItemWidth(-307);
            bool enterFindGL = ImGui::InputText("##InputGL", inputBuffer2, IM_ARRAYSIZE(inputBuffer2), ImGuiInputTextFlags_EnterReturnsTrue);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.4f, 0.15f, 1.0f));
            bool FindGLClicked = ImGui::Button("Find Game Name", ImVec2(135, 0));
            if (enterFindGL || FindGLClicked)
            {
                if (inputBuffer2[0] != '\0')
                {
                    gGProxy->m_BNET->SetSearchGameName(inputBuffer2);
                    CONSOLE_Print("[BNET] looking for a game named \"" + string(inputBuffer2) + "\" for up to two minutes");
                    inputBuffer2[0] = '\0';
                }
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
            bool RefClicked = ImGui::Button("Refresh Game List", ImVec2(155, 0));
            if (RefClicked)
            {
                gGProxy->m_BNET->QueueGetGameList(20);
                CONSOLE_Print(u8"[BNET] Làm mới danh sách trò chơi", dye_light_purple);
            }
            ImGui::PopStyleColor(3);


            // Settings section (trong tab Console)
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Spacing();

            if (gGProxy)
            {
                // Initialize buffers with current values (only once)
                if (!settingsInitialized)
                {
                    strcpy(serverBuffer, CFG.GetString("server", string()).c_str());
                    strcpy(usernameBuffer, CFG.GetString("username", string()).c_str());
                    strcpy(passwordBuffer, CFG.GetString("password", string()).c_str());

                    strcpy(botNameBuffer, CFG.GetString("BotName", string()).c_str());
                    strcpy(botMapBuffer, CFG.GetString("BotMap", string()).c_str());
                    portBuffer = gGProxy->m_Port;

                    // Detect current War3 version
                    uint32_t ver = gGProxy->m_War3Version;
                    if (ver == 24) selectedWar3Version = 0;
                    else if (ver == 26) selectedWar3Version = 1;
                    else if (ver == 27) selectedWar3Version = 2;
                    else if (ver == 28) selectedWar3Version = 3;
                    else if (ver == 29) selectedWar3Version = 4;
                    else if (ver == 31) selectedWar3Version = 5;

                    settingsInitialized = true;
                }

                float totalWidth = ImGui::GetContentRegionAvail().x;
                float leftWidth = totalWidth * 0.52f;
                float rightWidth = totalWidth * 0.46f;

                // === LEFT SIDE: Connection Settings ===
                ImGui::BeginGroup();
                ImGui::BeginChild("##LeftSettings", ImVec2(leftWidth, 175), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::Text("Server:  ");
                ImGui::SameLine();
                ImGui::PushItemWidth(-10);
                ImGui::InputText("##Server", serverBuffer, sizeof(serverBuffer));
                ImGui::PopItemWidth();

                ImGui::Text("Username:");
                ImGui::SameLine();
                ImGui::PushItemWidth(-10);
                ImGui::InputText("##Username", usernameBuffer, sizeof(usernameBuffer));
                ImGui::PopItemWidth();

                ImGui::Text("Password:");
                ImGui::SameLine();
                ImGui::PushItemWidth(-57);
                ImGui::InputText("##Password", passwordBuffer, sizeof(passwordBuffer), showPassword ? 0 : ImGuiInputTextFlags_Password);
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
                if (ImGui::Button(showPassword ? "(o)" : "(-)", ImVec2(40, 0))) {
                    showPassword = !showPassword;
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(showPassword ? "Hide password" : "Show password");
                }

                ImGui::Text("Port:");
                ImGui::SameLine();
                ImGui::PushItemWidth(120);
                if (ImGui::InputInt("##Port", &portBuffer))
                {
                    CFG.ReplaceKeyValue("port", to_string(portBuffer));
                }
                ImGui::PopItemWidth();

                // === BOTTOM: Connect button and checkboxes ===
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
                if (ImGui::Button("Connect", ImVec2(130, 28)))
                {
                    gGProxy->m_Server = serverBuffer;
                    gGProxy->m_Username = usernameBuffer;
                    gGProxy->m_Password = passwordBuffer;
                    gGProxy->m_Port = (uint16_t)portBuffer;
                    gGProxy->m_BNET->m_UserName = usernameBuffer;
                    gGProxy->m_BNET->m_UserPassword = passwordBuffer;
                    gGProxy->m_BNET->m_BNCSUtil->Reset(usernameBuffer, passwordBuffer);
                    gGProxy->m_BNET->m_Socket->Reset();
                    gGProxy->m_BNET->m_LastDisconnectedTime = GetTime();
                    gGProxy->m_BNET->m_LoggedIn = false;
                    gGProxy->m_BNET->m_InChat = false;
                    gGProxy->m_BNET->m_InGame = false;
                    gGProxy->m_BNET->m_WaitingToConnect = true;
                    gGProxy->m_BNET->SetTimerReconnect(1);

                    CFG.ReplaceKeyValue("server", serverBuffer);
                    CFG.ReplaceKeyValue("username", usernameBuffer);
                    CFG.ReplaceKeyValue("password", passwordBuffer);
                    CONSOLE_Print("[SYSTEM] Connecting to " + string(serverBuffer) + "...", dye_light_green);

                    if (gGProxy->m_BNET && gGProxy->m_BNET->m_Socket)
                    {
                        gGProxy->m_BNET->m_Socket->Disconnect();
                    }
                    scrollToBottom = true;
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();
                ImGui::Checkbox("Public Games", &gGProxy->m_PublicGames);
                ImGui::SameLine();
                ImGui::Checkbox("Filter GProxy++", &gGProxy->m_FilterGProxy);

                ImGui::EndChild();
                ImGui::EndGroup();

                ImGui::SameLine();

                // === RIGHT SIDE: War3 Version ===
                ImGui::BeginGroup();
                ImGui::BeginChild("##RightSettings", ImVec2(rightWidth, 80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Warcraft III Version");
                ImGui::Separator();

                const char* war3Versions[] = { "1.24", "1.26", "1.27", "1.28", "1.29", "1.31" };
                int previousVersion = selectedWar3Version;

                // All 6 items in ONE row
                for (int i = 0; i < 6; i++)
                {
                    if (ImGui::RadioButton(war3Versions[i], &selectedWar3Version, i))
                    {
                        // Version changed
                        if (selectedWar3Version != previousVersion)
                        {
                            const char* versionNumbers[] = { "24", "26", "27", "28", "29", "31" };
                            string newVersion = versionNumbers[selectedWar3Version];

                            CFG.ReplaceKeyValue("war3version", newVersion);
                            gGProxy->m_War3Version = atoi(newVersion.c_str());
                            gGProxy->m_BNET->m_War3Version = atoi(newVersion.c_str());

                            if (gGProxy->m_BNET && gGProxy->m_BNET->m_Socket)
                            {
                                gGProxy->m_BNET->SetTimerReconnect(1);
                                gGProxy->m_BNET->m_Socket->Disconnect();
                            }
                            //gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_PROTOCOL_INITIALIZE_SELECTOR());
                            //gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_AUTH_INFO(gGProxy->m_War3Version, gGProxy->m_TFT, gGProxy->m_BNET->m_CountryAbbrev, gGProxy->m_BNET->m_Country));
                            //gGProxy->m_BNET->m_Socket->DoSend((fd_set*)send_fd);
                            CONSOLE_Print(u8"[INFO] Đã chọn War3 version " + string(war3Versions[selectedWar3Version]), dye_light_green);
                        }
                    }

                    // All items on same line
                    if (i < 5)
                        ImGui::SameLine();
                }

                ImGui::EndChild();

                // === Bot Name and Host Button ===
                ImGui::Spacing();

                ImGui::BeginChild("##BotControls", ImVec2(rightWidth, 80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::Text("Bot:");
                ImGui::SameLine();
                ImGui::PushItemWidth(170);
                if (ImGui::InputText("##BotName", botNameBuffer, IM_ARRAYSIZE(botNameBuffer)))
                {
                    CFG.ReplaceKeyValue("BotName", botNameBuffer);
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();

                ImGui::Text("Map:");
                ImGui::SameLine();
                ImGui::PushItemWidth(170);
                if (ImGui::InputText("##BotMap", botMapBuffer, IM_ARRAYSIZE(botMapBuffer)))
                {
                    CFG.ReplaceKeyValue("BotMap", botMapBuffer);
                }
                ImGui::PopItemWidth();
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.8f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.7f, 1.0f));
                if (ImGui::Button("Host", ImVec2(70, 0)))
                {
                    if (strlen(botNameBuffer) <= 0)
                    {
                        CONSOLE_Print("[ERROR] Please enter Bot name!", dye_light_red);
                    }
                    else if (strlen(botMapBuffer) <= 0)
                    {
                        CONSOLE_Print("[ERROR] Please enter Map name!", dye_light_red);
                    }
                    else
                    {
                        string command = "/w " + string(botNameBuffer) + " !host " + string(botMapBuffer);
                        gInputBuffer = command;
                        CONSOLE_Print("[HOST] Sending command to " + string(botNameBuffer) + ": !host", dye_light_blue);
                    }
                }
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.4f, 0.8f, 1.0f));
                if (ImGui::Button("Host Obs", ImVec2(80, 0)))
                {
                    if (strlen(botNameBuffer) <= 0)
                    {
                        CONSOLE_Print("[ERROR] Please enter Bot name!", dye_light_red);
                    }
                    else if (strlen(botMapBuffer) <= 0)
                    {
                        CONSOLE_Print("[ERROR] Please enter Map name!", dye_light_red);
                    }
                    else
                    {
                        string command = "/w " + string(botNameBuffer) + " !host " + string(botMapBuffer);
                        gInputBuffer = command;
                        CONSOLE_Print("[HOST] Sending command to " + string(botNameBuffer) + ": !hostobs", dye_light_blue);
                    }
                }
                ImGui::PopStyleColor(3);

                ImGui::EndChild();
                ImGui::EndGroup();
            }

            ImGui::EndChild();

            // Channel sidebar
            ImGui::SameLine();

            ImGui::BeginChild("##ChannelSidebar", ImVec2(0, 0), true);

            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Channel");
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", gChannelName.c_str());
            ImGui::Text("Users: %d", (int)gChannelUsers.size());

            ImGui::Separator();

            for (const auto& user : gChannelUsers)
            {
                ImGui::Bullet();
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", user.c_str());
            }

            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Games"))
        {
            if (ImGui::Button("Refresh Game List", ImVec2(180, 35)))
            {
                if (gGProxy && gGProxy->m_BNET)
                {
                    gGProxy->m_BNET->m_TotalGames = 0;
					gGProxy->m_BNET->m_GameList.clear();
                    gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_REQUEST_GAME_LIST());
                }
            }

            ImGui::SameLine();
            ImGui::Text("Total Games: %d", gGProxy->m_BNET->m_TotalGames);

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::BeginChild("##GameList", ImVec2(0, 0), true);

            if (!gGProxy->m_BNET->m_GameList.empty())
            {
                ImGui::Columns(8, "gameColumns");

                ImGui::SetColumnWidth(0, 80.0f);  
                ImGui::SetColumnWidth(1, 220.0f); 
                ImGui::SetColumnWidth(2, 130.0f);
                ImGui::SetColumnWidth(3, 130.0f);
                ImGui::SetColumnWidth(4, 370.0f); 
                ImGui::SetColumnWidth(5, 80.0f);  
                ImGui::SetColumnWidth(6, 80.0f);  
                ImGui::SetColumnWidth(7, 140.0f);  

                ImGui::Separator();

                // Header
                TextCentered("Phiên \nbản"); ImGui::NextColumn();
                TextCentered(u8"Tên Game"); ImGui::NextColumn();
                TextCentered("Bot"); ImGui::NextColumn();
                TextCentered(u8"Tên Host"); ImGui::NextColumn();
                TextCentered("Map"); ImGui::NextColumn();
                TextCentered("Slots"); ImGui::NextColumn();
                TextCentered("Trạng \nThái"); ImGui::NextColumn();
                TextCentered("IP/Port"); ImGui::NextColumn();

                ImGui::Separator();

                for (auto& game : gGProxy->m_BNET->m_GameList)
                { 
                    //// Game Id
                    //ImGui::Text("%d", game.game_id);
                    //ImGui::NextColumn();

                    // Game version
                    ImGui::Text("%s", game.version);
                    ImGui::NextColumn();

                    // Game name
                    TextCentered(game.game_name);
                    ImGui::NextColumn();

					// bot name
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", game.host_name);
                    ImGui::NextColumn();

					// host name
                    std::string hostName = game.host_name;
                    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", game.owner_host_name);
                    ImGui::NextColumn();

                    // Map
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%s", game.map_name);
                    ImGui::NextColumn();

                    // Slots
                    ImGui::Text("%d / %d", game.current_players - 1, game.max_players);
                    ImGui::NextColumn();

                    // Status
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "%s", GetGameStatusString((t_game_status)game.game_status).c_str());
                    ImGui::NextColumn();

					// IP/Port
                    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "%s", game.IP_Port);
                    ImGui::NextColumn();

                    ImGui::Separator();
                }

                ImGui::Columns(1);
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f),
                    "No games found.");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                    "Click 'Refresh Game List' to load available games.");
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (gWaitingForGames) {
        uint32_t currentTime = GetTime();
        if (currentTime - gGamesStartTime > 5) {
            ParseGamesOutput(gGamesRawOutput);
            gGamesRawOutput.clear();
        }
    }

    ImGui::End();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_DESTROY:
    case WM_QUIT:
        gGProxy->m_Quit = true;
        break;

    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void GuiThread()
{
    g_ConsoleWindow = GetConsoleWindow();
    if (g_ConsoleWindow)
    {
        ShowWindow(g_ConsoleWindow, SW_HIDE);
        showConsoleWindow = false;
    }

    if (g_ConsoleWindow)
    {
        SetWindowPos(g_ConsoleWindow, HWND_BOTTOM, -10000, -10000, 0, 0, SWP_NOSIZE);
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);

    HICON hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    if (!hIcon)
        hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));

    if (!hIconSm)
        hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, 16, 16, 0);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("GProxyImGui"), NULL };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, _T("GProxy++ 3.2 New"), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 50, 50, 1280, 800, NULL, NULL, wc.hInstance, NULL);
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return;
    }

    if (hIcon)
    {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    if (hIconSm)
    {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
    }

    SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)hIcon);
    SetClassLongPtr(hwnd, GCLP_HICONSM, (LONG_PTR)hIconSm);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    InitImGuiConsole();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Font size (14px - compact for 1280x800)
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 15.0f, nullptr, io.Fonts->GetGlyphRangesVietnamese());

    bool done = false;
    std::string pendingCommand;

    while (!done)
    {
        if (gGProxy && gGProxy->m_Quit)
            break;

        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }

        if (done)
            break;

        if (!pendingCommand.empty())
        {
            Process_Command();
            pendingCommand.clear();
        }

        if (!gInputBuffer.empty())
        {
            pendingCommand = gInputBuffer;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderImGuiConsole();

        ImGui::EndFrame();

        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

        D3DCOLOR clear_color = D3DCOLOR_RGBA(15, 18, 22, 255);
        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            clear_color, 1.0f, 0);

        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }

        HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

        if (result == D3DERR_DEVICELOST &&
            g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
        {
            ResetDevice();
        }

        Sleep(16);
    }

    if (g_ConsoleWindow && showConsoleWindow)
        ShowWindow(g_ConsoleWindow, SW_SHOW);

    ShutdownImGuiConsole();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
}