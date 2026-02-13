#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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
#include "gproxy_map.h"
#include "resource1.h"
#include "MapUploadAPI.h"
#include "Include.h"
#include "vcredist_manager.h"
#include "getpass_api.h"
#include "srv_Url.h"
#include "Obuscate.hpp"

#include <d3d9.h>
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx9.h>
#include <imgui_internal.h>
#include <tchar.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <windows.h>
#include <sstream>
#include <unordered_map>
#include <algorithm>

#pragma comment(lib, "d3d9.lib")

#include "GDriveDownloader.h"
#pragma comment(lib, "gdown.lib")

void InitDonateQR();

// Globals
LPDIRECT3D9              g_pD3D = NULL;
LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
D3DPRESENT_PARAMETERS    g_d3dpp = {};
bool                     g_ImGuiInitialized = false;
ImFont* g_UnicodeFont = nullptr;

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
char serverBuffer[256] = "";
static char usernameBuffer[256] = "";
static char passwordBuffer[256] = "";
static char botNameBuffer[256] = "";
static char botAPINameBuffer[256] = "";
char botMapBuffer[256] = "";
char botAPIMapBuffer[256] = "";
static int portBuffer = 6112;
int selectedWar3Version = 0; // 0=1.24, 1=1.26, 2=1.27, 3=1.28, 4=1.29, 5=1.31
static bool settingsInitialized = false;
static bool showPassword = false;  // Toggle for password visibility

// Register popup variables
static bool showRegisterPopup = false;
static char regUsernameBuffer[256] = "";
static char regPasswordBuffer[256] = "";
static char regPasswordConfirmBuffer[256] = "";
static char regEmailBuffer[256] = "";
static bool regShowPassword = false;
static std::atomic<bool> g_RegisterInProgress(false);
static std::string g_RegisterResultMessage = "";
static bool g_RegisterSuccess = false;
static bool g_RegisterResultReady = false;
std::string g_PopupMessage = "";  // Message hiển thị trong popup
int g_PopupMessageType = 0;       // 0=none, 1=success, 2=error, 3=info
bool TabHostAPI = false;

static char g_GoogleDriveLinkBuffer[512] = "";
static int g_SelectedWar3VersionForLink = 3; // Default 1.28
static bool g_ShowUploadLinkPopup = false;

const char* war3Versions[] = { "1.24", "1.26", "1.27", "1.28", "1.29", "1.31" };
int32_t nwar3Versions[] = { 24, 26, 27, 28, 29, 31 };

LPDIRECT3DTEXTURE9 g_JpgTexture = nullptr;

// UI Ngon Ngu
int g_SelecteNgonNgu = 0;
static bool g_UploadLockDows = false;

// ============================================================================
// SETUP DOWNLOAD STATE
// ============================================================================
static std::atomic<bool> g_SetupDownloading(false);
static std::atomic<bool> g_SetupExtracting(false);
static GDrive::ProgressInfo g_SetupProgress;
static std::mutex g_SetupProgressMutex;
static GDrive::Downloader* g_SetupDownloader = nullptr;
static std::thread g_SetupDownloadThread;
static std::thread g_SetupExtractThread;
static char g_SetupDownloadURL[512] = "https://drive.google.com/file/d/1bLPMXQcxUiOf8tZkUwTH083c84ZGYA_u/view?usp=drive_link";
static std::string g_SetupLastError = "";
static bool g_SetupCompleted = false;
static std::string g_DownloadedFilePath = "";
static std::atomic<int> g_ExtractProgress(0);
static std::atomic<int> g_ExtractTotal(0);

string extractDriveID(const string& link)
{
    static const regex patterns[] =
    {
        regex("/file/d/([\\w-]{25,})"),      // /file/d/ID
        regex("/d/([\\w-]{25,})"),           // /d/ID (Docs, Sheets, Slides)
        regex("[?&]id=([\\w-]{25,})"),       // ?id=ID hoặc &id=ID
        regex("/folders/([\\w-]{25,})"),     // /folders/ID
        regex("^([\\w-]{25,})$")             // Chỉ có ID thuần (phải match toàn bộ string)
    };

    for (const auto& pat : patterns)
    {
        smatch m;
        if (regex_search(link, m, pat))
        {
            return m[1].str();  // Luôn lấy capturing group
        }
    }
    return "";
}

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

static std::string comboPreview = "Select bot";
static int currentBot = -1;
static bool botSynced = false;

enum BotStatus
{
    BOT_UNKNOWN,
    BOT_ONLINE,
    BOT_OFFLINE
};

BotStatus ParseBotStatus(const std::string& s)
{
    if (s.find("(online)") != std::string::npos)
        return BOT_ONLINE;
    if (s.find("(offline)") != std::string::npos)
        return BOT_OFFLINE;
    return BOT_UNKNOWN;
}

std::string StripBotStatus(const std::string& s)
{
    size_t p = s.find(" (");
    return (p != std::string::npos) ? s.substr(0, p) : s;
}

static MapUploadAPI* g_HostAPI = nullptr;

static std::vector<std::string> g_AdminList;
static std::mutex g_AdminListMutex;
static std::atomic<bool> g_AdminListLoading{ false };

static std::vector<std::string> g_BotList;
static std::mutex g_BotListMutex;
static std::atomic<bool> g_BotListLoading{ false };

void InitHostAPI()
{
    if (!g_HostAPI)
        g_HostAPI = new MapUploadAPI();
}

void LoadAdminListAsync()
{
    if (g_AdminListLoading) return;
    g_AdminListLoading = true;

    std::thread([]()
        {
            InitHostAPI();
            json adminList = g_HostAPI->GetAdminList();
            {
                std::lock_guard<std::mutex> lock(g_AdminListMutex);
                g_AdminList.clear();

                if (adminList.is_array())
                {
                    for (const auto& admin : adminList)
                    {
                        g_AdminList.push_back(admin.get<std::string>());
                    }
                }
            }

            g_AdminListLoading = false;
        }).detach();
}

void LoadBotListAsync()
{
    if (g_BotListLoading) return;
    g_BotListLoading = true;

    std::thread([]()
        {
            InitHostAPI();
            json botList = g_HostAPI->GetBotList();
            {
                std::lock_guard<std::mutex> lock(g_BotListMutex);
                g_BotList.clear();

                if (botList.is_array())
                {
                    for (const auto& bot : botList)
                    {
                        string botname = bot["name"].get<std::string>() + (bot["status"].get<std::string>() == "online" ? " (online)" : " (offline)");
                        g_BotList.push_back(botname);
                    }
                }
            }

            g_BotListLoading = false;
            botSynced = false;
        }).detach();
}

void SyncBotFromConfig()
{
    if (botAPINameBuffer[0] == '\0')
        return;

    for (size_t i = 0; i < g_BotList.size(); i++)
    {
        std::string name = StripBotStatus(g_BotList[i]);

        if (_stricmp(name.c_str(), botAPINameBuffer) == 0)
        {
            currentBot = i;
            comboPreview = name;
            return;
        }
    }

    currentBot = -1;
    comboPreview = "Select bot";
}

bool ComboFromStringVector(const char* label, int* current, const std::vector<std::string>& items)
{
    auto getter = [](void* data, int idx, const char** out_text)
        {
            auto& v = *(std::vector<std::string>*)data;
            *out_text = v[idx].c_str();
            return true;
        };

    return ImGui::Combo(label, current, getter, (void*)&items, items.size());
}

bool IsAdminUser(const std::string& szuser)
{
    for (const auto& user : g_AdminList)
    {
        if (szuser == user)
            return true;
    }
    return false;
}

// ============================================================================
// SETUP DOWNLOAD FUNCTIONS
// ============================================================================
void ExtractZipFile(const std::string& zipFilePath);

void SetupDownloadProgressCallback(const GDrive::ProgressInfo& info)
{
    std::lock_guard<std::mutex> lock(g_SetupProgressMutex);
    g_SetupProgress = info;
}

void SetupDownloadCompletionCallback(bool success, const std::string& filename,
    size_t filesize, double speed, const std::string& error)
{
    g_SetupDownloading = false;

    if (success)
    {
        g_DownloadedFilePath = filename;

        // Check if it's a ZIP file
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);

        if (lowerFilename.find(".zip") != std::string::npos)
        {
            // Start extraction automatically
            g_SetupExtracting = true;
            g_ExtractProgress = 0;
            g_ExtractTotal = 0;

            if (g_SetupExtractThread.joinable())
                g_SetupExtractThread.join();

            g_SetupExtractThread = std::thread([filename]()
                {
                    ExtractZipFile(filename);
                });
        }
        else
        {
            g_SetupCompleted = true;
            g_PopupMessage = u8"Tải hoàn tất!\nFile: " + filename +
                u8"\nKích thước: " + GDrive::Utils::FormatSize(filesize);
            g_PopupMessageType = 1; // success
        }
    }
    else
    {
        g_SetupCompleted = false;
        g_SetupLastError = error;
        g_PopupMessage = u8"Lỗi tải file: " + error;
        g_PopupMessageType = 2; // error
    }
}

void ExtractZipFile(const std::string& zipFilePath)
{
    try
    {
        // Get filename without extension for folder name
        size_t lastDot = zipFilePath.find_last_of('.');
        size_t lastSlash = zipFilePath.find_last_of("/\\");

        std::string folderName;
        if (lastDot != std::string::npos && lastSlash != std::string::npos)
        {
            folderName = zipFilePath.substr(lastSlash + 1, lastDot - lastSlash - 1);
        }
        else if (lastDot != std::string::npos)
        {
            folderName = zipFilePath.substr(0, lastDot);
        }
        else
        {
            folderName = zipFilePath + "_extracted";
        }

        // Get full paths
        char fullZipPath[MAX_PATH];
        char fullFolderPath[MAX_PATH];
        GetFullPathNameA(zipFilePath.c_str(), MAX_PATH, fullZipPath, NULL);
        GetFullPathNameA(folderName.c_str(), MAX_PATH, fullFolderPath, NULL);

        // Create extraction directory
        CreateDirectoryA(fullFolderPath, NULL);

        // Use simpler PowerShell Expand-Archive command
        std::string command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"";
        command += "try { ";
        command += "$ProgressPreference = 'SilentlyContinue'; ";
        command += "Expand-Archive -Path '" + std::string(fullZipPath) + "' ";
        command += "-DestinationPath '" + std::string(fullFolderPath) + "' -Force; ";
        command += "Write-Host 'SUCCESS'; ";
        command += "} catch { ";
        command += "Write-Host \"ERROR: $_\"; ";
        command += "exit 1; ";
        command += "}\"";

        // Execute PowerShell command
        STARTUPINFOA si = { sizeof(STARTUPINFOA) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = {};

        // Create process
        BOOL result = CreateProcessA(
            NULL,
            (LPSTR)command.c_str(),
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi
        );

        if (result)
        {
            // Set initial progress
            g_ExtractProgress = 0;
            g_ExtractTotal = 100;

            // Wait for completion with progress simulation
            DWORD waitResult;
            int progress = 0;
            do
            {
                waitResult = WaitForSingleObject(pi.hProcess, 500);

                if (waitResult == WAIT_TIMEOUT)
                {
                    // Simulate progress (since we can't get real progress easily)
                    progress += 10;
                    if (progress > 90) progress = 90;
                    g_ExtractProgress = progress;
                }
            } while (waitResult == WAIT_TIMEOUT);

            // Get exit code
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            g_ExtractProgress = 100;
            g_ExtractTotal = 100;

            if (exitCode == 0)
            {
                g_SetupExtracting = false;
                g_SetupCompleted = true;
                g_PopupMessage = u8"Giải nén hoàn tất!\nThư mục: " + folderName;
                g_PopupMessageType = 1; // success
            }
            else
            {
                g_SetupExtracting = false;
                g_SetupCompleted = false;
                g_PopupMessage = u8"Lỗi giải nén file ZIP (Exit code: " + std::to_string(exitCode) + ")";
                g_PopupMessageType = 2; // error
            }
        }
        else
        {
            DWORD error = GetLastError();
            g_SetupExtracting = false;
            g_SetupCompleted = false;
            g_PopupMessage = u8"Lỗi khởi chạy PowerShell (Error: " + std::to_string(error) + ")";
            g_PopupMessageType = 2; // error
        }
    }
    catch (const std::exception& e)
    {
        g_SetupExtracting = false;
        g_SetupCompleted = false;
        g_PopupMessage = u8"Lỗi giải nén: " + std::string(e.what());
        g_PopupMessageType = 2; // error
    }
}

void StartSetupDownload()
{
    if (g_SetupDownloading)
        return;

    g_SetupDownloading = true;
    g_SetupCompleted = false;
    g_SetupLastError = "";

    // Reset progress
    {
        std::lock_guard<std::mutex> lock(g_SetupProgressMutex);
        g_SetupProgress = GDrive::ProgressInfo();
    }

    // Start download thread
    if (g_SetupDownloadThread.joinable())
        g_SetupDownloadThread.join();

    g_SetupDownloadThread = std::thread([]()
        {
            try
            {
                // Initialize downloader
                if (!g_SetupDownloader)
                    g_SetupDownloader = new GDrive::Downloader();

                // Set callbacks
                g_SetupDownloader->SetProgressCallback(SetupDownloadProgressCallback);
                g_SetupDownloader->SetCompletionCallback(SetupDownloadCompletionCallback);

                // Configure download options
                GDrive::DownloadOptions options;
                options.showProgress = true;
                options.outputDir = ".";
                options.bufferSize = 2 * 1024 * 1024; // 2MB buffer

                // Extract file ID from URL
                std::string fileId = GDrive::Downloader::ExtractFileId(g_SetupDownloadURL);

                // Download file
                GDrive::DownloadResult result = g_SetupDownloader->DownloadFile(fileId, options);

                // Callback will be called automatically
            }
            catch (const std::exception& e)
            {
                SetupDownloadCompletionCallback(false, "", 0, 0, e.what());
            }
        });
}

void CancelSetupDownload()
{
    if (g_SetupDownloading)
    {
        // Get current filename before resetting
        std::string downloadingFile;
        {
            std::lock_guard<std::mutex> lock(g_SetupProgressMutex);
            downloadingFile = g_SetupProgress.filename;
        }

        // Set flag to stop download immediately
        g_SetupDownloading = false;

        // Try to cancel the downloader
        if (g_SetupDownloader)
        {
            g_SetupDownloader->Cancel();
        }

        // Reset progress immediately so UI updates
        {
            std::lock_guard<std::mutex> lock(g_SetupProgressMutex);
            g_SetupProgress = GDrive::ProgressInfo();
        }

        // Detach the download thread (let it finish in background)
        // This prevents blocking UI while CURL finishes
        if (g_SetupDownloadThread.joinable())
        {
            g_SetupDownloadThread.detach();
        }

        // Reset downloader for next use
        if (g_SetupDownloader)
        {
            delete g_SetupDownloader;
            g_SetupDownloader = nullptr;
        }

        // Delete partial file after a short delay
        if (!downloadingFile.empty())
        {
            std::thread([downloadingFile]() {
                // Wait a bit for CURL to close file handle
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                // Try to delete the partial file
                if (DeleteFileA(downloadingFile.c_str()))
                {
                    // Success - file deleted
                }
                else
                {
                    // File might still be locked, try again
                    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                    DeleteFileA(downloadingFile.c_str());
                }
                }).detach();
        }

        // Show cancellation message
        g_PopupMessage = u8"Đã hủy tải file";
        g_PopupMessageType = 3; // info
    }
}

void RenderSetupTab()
{
    ImGui::BeginChild("##SetupTabContent", ImVec2(0, 0), false);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), u8"CÀI ĐẶT TỰ ĐỘNG");
    ImGui::Separator();
    ImGui::Spacing();

    // Google Drive URL input
    ImGui::Text(u8"Google Drive URL:");
    ImGui::SetNextItemWidth(-1);

    // Disable input when downloading or extracting
    if (g_SetupDownloading || g_SetupExtracting)
        ImGui::BeginDisabled();

    ImGui::InputText("##SetupURL", g_SetupDownloadURL, sizeof(g_SetupDownloadURL));

    if (g_SetupDownloading || g_SetupExtracting)
        ImGui::EndDisabled();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(u8"Nhập link Google Drive chứa file cài đặt\nVí dụ: https://drive.google.com/file/d/YOUR_FILE_ID/view");

    ImGui::Spacing();

    // Download button, progress bar, or extraction progress
    if (!g_SetupDownloading && !g_SetupExtracting)
    {
        // Show setup button
        ImVec4 buttonColor = ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
        ImVec4 buttonHovered = ImVec4(0.3f, 0.8f, 0.3f, 1.0f);
        ImVec4 buttonActive = ImVec4(0.1f, 0.6f, 0.1f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActive);

        bool canDownload = strlen(g_SetupDownloadURL) > 10;

        if (!canDownload)
            ImGui::BeginDisabled();

        if (ImGui::Button(u8"▶ BẮT ĐẦU CÀI ĐẶT", ImVec2(-1, 40)))
        {
            StartSetupDownload();
        }

        if (!canDownload)
            ImGui::EndDisabled();

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered() && canDownload)
            ImGui::SetTooltip(u8"Click để tải và cài đặt file từ Google Drive");
    }
    else if (g_SetupDownloading)
    {
        // Show download progress bar
        GDrive::ProgressInfo progress;
        {
            std::lock_guard<std::mutex> lock(g_SetupProgressMutex);
            progress = g_SetupProgress;
        }

        // Check if download completed but callback not triggered
        static bool completionTriggered = false;
        if (progress.percent >= 100 && !progress.filename.empty() && !completionTriggered)
        {
            completionTriggered = true;

            // Manually trigger completion after a short delay to ensure download finished
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                GDrive::ProgressInfo finalProgress;
                {
                    std::lock_guard<std::mutex> lock(g_SetupProgressMutex);
                    finalProgress = g_SetupProgress;
                }

                if (!finalProgress.filename.empty())
                {
                    SetupDownloadCompletionCallback(true, finalProgress.filename,
                        finalProgress.totalSize, finalProgress.speed, "");
                }
                }).detach();
        }
        else if (progress.percent < 100)
        {
            completionTriggered = false;
        }

        // Progress info text
        if (!progress.filename.empty())
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), u8"Đang tải: %s", progress.filename.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), u8"Đang khởi tạo...");
        }

        ImGui::Spacing();

        // Progress bar
        float progressFraction = progress.percent / 100.0f;
        ImGui::ProgressBar(progressFraction, ImVec2(-1, 30));

        // Progress details
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::CalcTextSize("100%").x - 40);
        ImGui::Text("%d%%", progress.percent);

        ImGui::Spacing();

        // Speed and size info
        ImGui::Text(u8"Tốc độ: %.2f MB/s", progress.speed);
        ImGui::SameLine(200);
        ImGui::Text(u8"Đã tải: %.1f / %.1f MB", progress.mbDownloaded, progress.mbTotal);

        ImGui::Spacing();

        // Cancel button (only show when downloading, not when complete)
        if (progress.percent < 100)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));

            if (ImGui::Button(u8"✖ HỦY TẢI", ImVec2(-1, 30)))
            {
                CancelSetupDownload();
            }

            ImGui::PopStyleColor(3);
        }
    }
    else if (g_SetupExtracting)
    {
        // Show extraction progress
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), u8"Đang giải nén: %s", g_DownloadedFilePath.c_str());

        ImGui::Spacing();

        // Extraction progress bar
        int extractProgress = g_ExtractProgress.load();
        int extractTotal = g_ExtractTotal.load();

        float extractFraction = (extractTotal > 0) ? ((float)extractProgress / extractTotal) : 0.0f;

        ImGui::ProgressBar(extractFraction, ImVec2(-1, 30));

        ImGui::SameLine();
        if (extractTotal > 0)
        {
            int percent = (int)(extractFraction * 100);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::CalcTextSize("100%").x - 40);
            ImGui::Text("%d%%", percent);
        }

        ImGui::Spacing();

        // Files extracted info
        if (extractTotal > 0)
        {
            ImGui::Text(u8"Đã giải nén: %d / %d files", extractProgress, extractTotal);
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), u8"Đang chuẩn bị giải nén...");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Instructions
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), u8"HƯỚNG DẪN:");
    ImGui::BulletText(u8"Nhập link Google Drive chứa file cài đặt");
    ImGui::BulletText(u8"Click 'Bắt đầu cài đặt' để tải file");
    ImGui::BulletText(u8"File ZIP sẽ tự động giải nén vào thư mục cùng tên");
    ImGui::BulletText(u8"File khác sẽ được tải về thư mục hiện tại");

    ImGui::EndChild();
}

// ============================================================================
// REGISTER API FUNCTIONS
// ============================================================================
struct RegisterResult {
    bool success;
    std::string message;
    int code;
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

RegisterResult CallRegisterAPI(const std::string& username, const std::string& password, const std::string& email)
{
    RegisterResult result = { false, "", 0 };

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result.message = "Failed to initialize CURL";
        return result;
    }

    std::string postData = "username=" + username + "&password=" + password;
    if (!email.empty()) 
    {
        postData += "&email=" + email;
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, ServerConfig::REGISTER_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: GProxy/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        result.message = "Request failed: " + std::string(curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return result;
    }

    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    result.code = (int)statusCode;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (response.find("\"success\":true") != std::string::npos || response.find("\"success\": true") != std::string::npos)
        result.success = true;

    size_t msgStart = response.find("\"message\":\"");
    if (msgStart != std::string::npos)
    {
        msgStart += 11;
        size_t msgEnd = response.find("\"", msgStart);
        if (msgEnd != std::string::npos)
            result.message = response.substr(msgStart, msgEnd - msgStart);
    }

    if (result.message.empty())
    {
        if (result.success)
        {
            result.message = "Account created successfully!";
        }
        else if (result.code == 409)
        {
            result.message = "Username already exists!";
        }
        else if (result.code == 400)
        {
            result.message = "Invalid input data!";
        }
        else
        {
            result.message = "Registration failed (HTTP " + std::to_string(result.code) + ")";
        }
    }

    return result;
}

void RegisterThreadFunc(std::string username, std::string password, std::string email)
{
    RegisterResult result = CallRegisterAPI(username, password, email);

    g_RegisterSuccess = result.success;
    g_RegisterResultMessage = result.message;
    g_RegisterResultReady = true;
    g_RegisterInProgress = false;
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
    if (g_JpgTexture)
    {
        g_JpgTexture->Release();
        g_JpgTexture = nullptr;
    }

    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (FAILED(hr))
        return;

    ImGui_ImplDX9_CreateDeviceObjects();

    InitDonateQR();
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

    InitDonateQR();

    // Initialize Google Drive Downloader
    GDrive::Downloader::Initialize();
}

void ShutdownImGuiConsole()
{
    if (!g_ImGuiInitialized)
        return;

    // Cleanup Google Drive Downloader
    if (g_SetupDownloader)
    {
        if (g_SetupDownloading)
            g_SetupDownloader->Cancel();

        if (g_SetupDownloadThread.joinable())
            g_SetupDownloadThread.join();

        delete g_SetupDownloader;
        g_SetupDownloader = nullptr;
    }

    // Cleanup extraction thread
    if (g_SetupExtractThread.joinable())
        g_SetupExtractThread.join();

    GDrive::Downloader::Cleanup();

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_ImGuiInitialized = false;
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
set URL=http://thaison1995.pro/gproxy.exe
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

void ProcessUploadMapLink(const std::string& googleDriveLink, std::string mappass, int war3Version, bool bUploadLockDows)
{
    if (!mappass.empty())
        mappass = AdvB64::encode(mappass);

    std::string command = "/w ";
    command += botNameBuffer;
    if (bUploadLockDows && !mappass.empty())
    {
        command += " !dowsp";  // Lock + Pass
    }
    else if (!mappass.empty())
    {
        command += " !dowp";   // Pass only
    }
    else if (bUploadLockDows)
    {
        command += " !dows";   // Lock only
    }
    else
    {
        command += " !dow";    // Normal
    }
    command += std::to_string(nwar3Versions[war3Version]) + " ";
    command += mappass.empty() ? googleDriveLink : (mappass + " " + googleDriveLink);

    gInputBuffer = command;
}

void RenderUploadMapLinkPopup()
{
    // Mở popup khi cần
    if (g_ShowUploadLinkPopup)
    {
        ImGui::OpenPopup("Upload Map Link");
        g_ShowUploadLinkPopup = false;
        g_ShowPasswordTextUpload = false;
        memset(g_mappassword, 0, sizeof(g_mappassword));
    }

    // Center popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Upload Map Link", NULL, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Upload Map từ Google Drive Link");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Google Drive Link Input
        ImGui::Text("Google Drive Link:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##GoogleDriveLink", "https://drive.google.com/file/d/...", g_GoogleDriveLinkBuffer, 512);

        ImGui::Spacing();

        // War3 Version Dropdown
        ImGui::Text(g_SelecteNgonNgu ? "Version War3" : "Phiên bản Warcraft III:");
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##War3Version", &g_SelectedWar3VersionForLink, war3Versions, IM_ARRAYSIZE(war3Versions));

        ImGui::SameLine();
        ImGui::Checkbox(g_SelecteNgonNgu ? "Lock Download Map" : "Khoá tải map về", &g_UploadLockDows);

        RenderInputTextMapPassword();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Help text
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped(g_SelecteNgonNgu ? "Note: The link must be a direct download link from Google Drive" : "Lưu ý: Link phải là direct download link từ Google Drive");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Buttons
        float buttonWidth = 120.0f;
        float spacing = 10.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

        // Send button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.3f, 1.0f));

        bool canSend = strlen(g_GoogleDriveLinkBuffer) > 0;
        if (!canSend)
        {
            ImGui::BeginDisabled(true);
        }

        if (ImGui::Button(g_SelecteNgonNgu ? "Send" : "Gửi", ImVec2(buttonWidth, 35)))
        {
            string strID = extractDriveID(g_GoogleDriveLinkBuffer);
            ProcessUploadMapLink(strID, g_mappassword, g_SelectedWar3VersionForLink, g_UploadLockDows);

            // Clear và đóng popup
            g_GoogleDriveLinkBuffer[0] = '\0';
            g_SelectedWar3VersionForLink = 3; // Reset về 1.28
            g_UploadLockDows = false;
            ImGui::CloseCurrentPopup();
        }

        if (!canSend)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip(g_SelecteNgonNgu ? "Please enter the Google Drive link" : "Vui lòng nhập Google Drive link");
            }
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, spacing);

        // Cancel button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

        if (ImGui::Button(g_SelecteNgonNgu ? "Cancel" : "Hủy", ImVec2(buttonWidth, 35)))
        {
            g_GoogleDriveLinkBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    }
}

void ButtonHost(bool hostapi, int war3Version)
{
    string cHost = hostapi ? " !hosts" : " !host";

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.7f, 1.0f));
    if (ImGui::Button("Host", ImVec2(70, 0)))
    {
        if (strlen(hostapi ? botAPINameBuffer : botNameBuffer) <= 0)
        {
            CONSOLE_Print("[ERROR] Please enter Bot name!", dye_light_red);
        }
        else if (strlen(hostapi ? botAPIMapBuffer : botMapBuffer) <= 0)
        {
            CONSOLE_Print("[ERROR] Please enter Map name!", dye_light_red);
        }
        else
        {
            cHost += to_string(war3Version) + " ";
            string command = "/w " + string(hostapi ? botAPINameBuffer : botNameBuffer) + cHost + string(hostapi ? botAPIMapBuffer : botMapBuffer);
            gInputBuffer = command;
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.4f, 0.8f, 1.0f));
    if (ImGui::Button("Host Obs", ImVec2(80, 0)))
    {
        if (strlen(hostapi ? botAPINameBuffer : botNameBuffer) <= 0)
        {
            CONSOLE_Print("[ERROR] Please enter Bot name!", dye_light_red);
        }
        else if (strlen(hostapi ? botAPINameBuffer : botNameBuffer) <= 0)
        {
            CONSOLE_Print("[ERROR] Please enter Map name!", dye_light_red);
        }
        else
        {
			cHost += "obs";
            cHost += to_string(war3Version) + " ";
            string command = "/w " + string(hostapi ? botAPINameBuffer : botNameBuffer) + cHost + string(hostapi ? botAPIMapBuffer : botMapBuffer);
            gInputBuffer = command;
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.35f, 0.35f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.45f, 0.45f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.25f, 0.25f, 1.00f));
    if (ImGui::Button("Unhost", ImVec2(80, 0)))
    {
        if (strlen(hostapi ? botAPINameBuffer : botNameBuffer) <= 0)
        {
            CONSOLE_Print("[ERROR] Please enter Bot name!", dye_light_red);
        }
        else
        {
            string command = "/w " + string(hostapi ? botAPINameBuffer : botNameBuffer) + " !unhost";
            gInputBuffer = command;
            CONSOLE_Print("[HOST] Sending command to " + string(hostapi ? botAPINameBuffer : botNameBuffer) + ": !unhost", dye_light_blue);
        }
    }
    ImGui::PopStyleColor(3);

    if (hostapi == true)
    {
        bool isAdmin = IsAdminUser(GGetUserName());
        if (!isAdmin)
        {
            ImGui::BeginDisabled(true);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.3f, 1.0f));
        if (ImGui::Button("Upload Map"))
        {
            ClearMessages();
            showUploadConfigPopup = true;
            g_ShowPasswordTextUpload = false;
            memset(g_mappassword, 0, sizeof(g_mappassword));
            ImGui::OpenPopup("Upload Map/Config");
        }
        if (!isAdmin && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(u8"Có trong List Admin Host API\nmới sử dụng được tính năng này");
        }
        ImGui::PopStyleColor(3);

        if (!isAdmin)
        {
            ImGui::EndDisabled();
        }
    }
    else
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.3f, 1.0f));
        if (ImGui::Button("Upload Map"))
        {
            if (strlen(botNameBuffer) <= 0)
            {
                CONSOLE_Print("[ERROR] Please enter Bot name!", dye_light_red);
            }
            else g_ShowUploadLinkPopup = true;
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text(u8"Upload map từ Google Drive link");
            ImGui::EndTooltip();
        }
    }
}

bool AnimatedButton_MirrorSweep(const char* label, ImVec2 size)
{
    static std::unordered_map<ImGuiID, float> anim;
    ImGuiID id = ImGui::GetID(label);
    float& t = anim[id];

    bool pressed = ImGui::Button(label, size);

    // LẤY RECT THẬT CỦA BUTTON
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    bool hovered = ImGui::IsItemHovered();

    if (hovered)
        t += ImGui::GetIO().DeltaTime * 1.0f;
    else
        t = 0.0f;

    if (t > 1.2f)
        t = 0.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    float width = max.x - min.x;
    float height = max.y - min.y;

    float sweepWidth = width * 0.5f;
    float x = min.x + (t * (width + sweepWidth)) - sweepWidth;

    dl->PushClipRect(min, max, true);

    ImVec2 smin(x, min.y);
    ImVec2 smax(x + sweepWidth, max.y);

    dl->AddRectFilledMultiColor(
        smin,
        smax,
        ImColor(1.0f, 1.0f, 1.0f, 0.0f),
        ImColor(1.0f, 1.0f, 1.0f, 0.7f),
        ImColor(1.0f, 1.0f, 1.0f, 0.7f),
        ImColor(1.0f, 1.0f, 1.0f, 0.0f)
    );

    dl->PopClipRect();

    return pressed;
}

unsigned char* DecodeJPGFromMPQ(const char* mpqFileName, int* width, int* height)
{
    unsigned char* jpgData = nullptr;
    DWORD jpgSize = 0;

    if (!LoadFileFromMPQ(mpqFileName, &jpgData, &jpgSize))
        return nullptr;

    int channels = 0;
    unsigned char* rgba = stbi_load_from_memory(jpgData, jpgSize, width, height, &channels, 4);

    delete[] jpgData; // Free MPQ buffer

    return rgba;
}

LPDIRECT3DTEXTURE9 CreateTextureFromRGBA(LPDIRECT3DDEVICE9 device, unsigned char* rgba, int width, int height)
{
    LPDIRECT3DTEXTURE9 texture = nullptr;

    if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr)))
        return nullptr;

    D3DLOCKED_RECT rect;
    if (FAILED(texture->LockRect(0, &rect, nullptr, 0)))
    {
        texture->Release();
        return nullptr;
    }

    // Copy và convert RGBA -> BGRA cho DX9
    unsigned char* dest = (unsigned char*)rect.pBits;

    for (int y = 0; y < height; y++)
    {
        unsigned char* srcRow = rgba + y * width * 4;
        unsigned char* dstRow = dest + y * rect.Pitch;

        for (int x = 0; x < width; x++)
        {
            int srcIdx = x * 4;
            int dstIdx = x * 4;

            dstRow[dstIdx + 0] = srcRow[srcIdx + 2]; // B
            dstRow[dstIdx + 1] = srcRow[srcIdx + 1]; // G
            dstRow[dstIdx + 2] = srcRow[srcIdx + 0]; // R
            dstRow[dstIdx + 3] = srcRow[srcIdx + 3]; // A
        }
    }

    texture->UnlockRect(0);

    return texture;
}

LPDIRECT3DTEXTURE9 LoadJPGTextureFromMPQ(LPDIRECT3DDEVICE9 device, const char* mpqFileName)
{
    int w, h;
    unsigned char* rgba = DecodeJPGFromMPQ(mpqFileName, &w, &h);

    if (!rgba)
        return nullptr;

    auto tex = CreateTextureFromRGBA(device, rgba, w, h);

    stbi_image_free(rgba);

    return tex;
}

void InitDonateQR()
{
    if (!g_JpgTexture)
    {
        g_JpgTexture = LoadJPGTextureFromMPQ(g_pd3dDevice, "donate.jpg");
    }

    g_SelecteNgonNgu = CFG.GetInt("ngon_ngu", 0);
}

void RenderImGuiConsole()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##MainWindow", nullptr, flags);

    // Title bar
    string versionString = "GProxy++ " + string(GPROXY_VERSION) + " New By Thái Sơn";
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), versionString.c_str());

    ImGui::SameLine(ImGui::GetWindowWidth() - 585);

    ImGui::SetNextItemWidth(130.0f);
    const char* strngongu[] = { "Tiếng Việt", "English" };
    if (ImGui::Combo("##NgonNgu", &g_SelecteNgonNgu, strngongu, IM_ARRAYSIZE(strngongu)))
    {
        CFG.ReplaceKeyValue("ngon_ngu", to_string(g_SelecteNgonNgu));
    }

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.65f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.45f, 0.08f, 1.0f));
    if (ImGui::Button("Donate", ImVec2(140, 0)))
    {
        ImGui::OpenPopup("DonatePopup");
    }
    ImGui::PopStyleColor(3);

    // ==== Donate Popup ====
    ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("DonatePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Support GProxy Project");

        ImGui::Separator();
        ImGui::Spacing();

        if (g_JpgTexture)
        {
            ImGui::Image((ImTextureID)g_JpgTexture, ImVec2(350, 500));
        }

        // Close button center
        float btnWidth = 100.0f;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btnWidth) * 0.5f);

        if (ImGui::Button("Close", ImVec2(btnWidth, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
    if (ImGui::Button(g_SelecteNgonNgu ? "Update Gproxy" : "Cập nhật", ImVec2(140, 0)))
    {
        CreateUpdaterBat();
        if (gGProxy)
        {
            gGProxy->m_Quit = true;
            ShellExecuteA(NULL, "open", "update.bat", NULL, NULL, SW_SHOW);
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button(g_SelecteNgonNgu ? "Exit" : "Thoát", ImVec2(140, 0)))
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
        if (ImGui::BeginTabItem(g_SelecteNgonNgu ? "Gernal" : "Chung"))
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
                gGProxy->m_BNET->QueueGetGameList(100);
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
                    InitHostAPI();
                    LoadAdminListAsync();
                    LoadBotListAsync();

                    strcpy(serverBuffer, CFG.GetString("server", "thaison1995.pro").c_str());
                    strcpy(usernameBuffer, CFG.GetString("username", string()).c_str());
                    strcpy(passwordBuffer, CFG.GetString("password", string()).c_str());

                    strcpy(botNameBuffer, CFG.GetString("BotName", string()).c_str());
                    strcpy(botMapBuffer, CFG.GetString("BotMap", string()).c_str());

                    strcpy(botAPINameBuffer, CFG.GetString("BotAPIName", string()).c_str());
                    strcpy(botAPIMapBuffer, CFG.GetString("BotAPIMap", string()).c_str());

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
                float leftWidth = totalWidth * 0.48f;
                float rightWidth = totalWidth * 0.51f;
                float rightWidth2 = totalWidth * 0.71f;

                // === LEFT SIDE: Connection Settings ===
                ImGui::BeginGroup();
                ImGui::BeginChild("##LeftSettings", ImVec2(leftWidth, 175), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::Text(g_SelecteNgonNgu ? "Server: " : "Máy Chủ:  ");
                ImGui::SameLine();
                ImGui::PushItemWidth(-10);
                ImGui::InputText("##Server", serverBuffer, sizeof(serverBuffer));
                ImGui::PopItemWidth();

                ImGui::Text(g_SelecteNgonNgu ? "Account:" : "Tài Khoản:");
                ImGui::SameLine();
                ImGui::PushItemWidth(-10);
                ImGui::InputText("##Username", usernameBuffer, sizeof(usernameBuffer));
                ImGui::PopItemWidth();

                ImGui::Text(g_SelecteNgonNgu ? "Password: " : "Mật Khẩu:");
                ImGui::SameLine();
                ImGui::PushItemWidth(-57);
                ImGui::InputText("##Password", passwordBuffer, sizeof(passwordBuffer), showPassword ? 0 : ImGuiInputTextFlags_Password);
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
                if (ImGui::Button(showPassword ? "(o)" : "(-)", ImVec2(40, 0)))
                {
                    showPassword = !showPassword;
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                {
                    const char* sdasasd = g_SelecteNgonNgu ? "Hide password" : "Ẩn mật khẩu";
                    const char* sdasasd2 = g_SelecteNgonNgu ? "Show password" : "Hiện mật khẩu";
                    ImGui::SetTooltip(showPassword ? sdasasd : sdasasd2);
                }

                ImGui::Text(g_SelecteNgonNgu ? "Port:" : "Cổng:");
                ImGui::SameLine();
                ImGui::PushItemWidth(120);
                if (ImGui::InputInt("##Port", &portBuffer))
                {
                    CFG.ReplaceKeyValue("port", to_string(portBuffer));
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::Checkbox("Public Games", &gGProxy->m_PublicGames);
                //ImGui::SameLine();
                //ImGui::Checkbox("Filter GProxy++", &gGProxy->m_FilterGProxy);

                // === BOTTOM: Connect button and checkboxes ===
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));
                if (ImGui::Button(g_SelecteNgonNgu ? "Log in" : "Đăng nhập", ImVec2(100, 28)))
                {
                    gGProxy->m_Server = serverBuffer;
                    gGProxy->m_Username = usernameBuffer;
                    gGProxy->m_Password = passwordBuffer;
                    gGProxy->m_Port = (uint16_t)portBuffer;
                    gGProxy->m_BNET->m_Server = serverBuffer;
                    gGProxy->m_BNET->m_ServerIP = serverBuffer;
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

                // Register Button - Opens popup
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.55f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.65f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.45f, 0.05f, 1.0f));
                if (ImGui::Button(g_SelecteNgonNgu ? "Register" : "Đăng ký", ImVec2(100, 28)))
                {
                    // Reset form
                    strcpy(regUsernameBuffer, "");
                    strcpy(regPasswordBuffer, "");
                    strcpy(regPasswordConfirmBuffer, "");
                    strcpy(regEmailBuffer, "");
                    g_PopupMessage = "";
                    g_PopupMessageType = 0;
                    showRegisterPopup = true;
                    ImGui::OpenPopup(g_SelecteNgonNgu ? "Register an account" : "Đăng ký tài khoản");
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(g_SelecteNgonNgu ? "Register a new account" : "Đăng ký tài khoản mới");
                }

                ImGui::SameLine();

                // Get Password Button - Opens popup
                ImVec4 link = ImVec4(0.40f, 0.75f, 1.00f, 1.0f);
                ImVec4 linkHover = ImVec4(0.55f, 0.82f, 1.00f, 1.0f);
                ImVec4 linkActive = ImVec4(0.30f, 0.65f, 0.95f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, link);
                if (ImGui::Button(g_SelecteNgonNgu ? "Forgot password" : "Quên mật khẩu", ImVec2(120, 28)))
                {
                    InitCall();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 size = ImGui::GetItemRectSize();
                    ImVec2 max = ImGui::GetItemRectMax();

                    ImVec2 lineStart = ImVec2(min.x, min.y + size.y);
                    ImVec2 lineEnd = max;

                    ImGui::GetWindowDrawList()->AddLine(lineStart, lineEnd, ImGui::GetColorU32(linkHover), 1.0f);
                }
                ImGui::PopStyleColor(4);
                RenderGetPassPopup();

                // ============================================
                // REGISTER POPUP MODAL
                // ============================================
                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(ImVec2(420, 320));

                if (ImGui::BeginPopupModal(g_SelecteNgonNgu ? "Register an account" : "Đăng ký tài khoản", &showRegisterPopup, ImGuiWindowFlags_NoResize))
                {
                    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), g_SelecteNgonNgu ? "Server: %s" : "Máy chủ: %s", serverBuffer);
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Username
                    ImGui::Text(g_SelecteNgonNgu ? "Account:" :"Tài Khoản:");
                    ImGui::SameLine(100);
                    ImGui::PushItemWidth(-10);
                    ImGui::InputText("##RegUsername", regUsernameBuffer, sizeof(regUsernameBuffer));
                    ImGui::PopItemWidth();

                    // Password
                    ImGui::Text(g_SelecteNgonNgu ? "Password:" : "Mật Khẩu:");
                    ImGui::SameLine(100);
                    ImGui::PushItemWidth(-50);
                    ImGui::InputText("##RegPassword", regPasswordBuffer, sizeof(regPasswordBuffer),
                        regShowPassword ? 0 : ImGuiInputTextFlags_Password);
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button(regShowPassword ? "(o)" : "(-)", ImVec2(35, 0))) {
                        regShowPassword = !regShowPassword;
                    }

                    // Confirm Password
                    ImGui::Text(g_SelecteNgonNgu ? "Confirm\nPassword:" : "Xác nhận\nMật Khẩu:");
                    ImGui::SameLine(100);
                    ImGui::PushItemWidth(-10);
                    ImGui::InputText("##RegPasswordConfirm", regPasswordConfirmBuffer, sizeof(regPasswordConfirmBuffer), regShowPassword ? 0 : ImGuiInputTextFlags_Password);
                    ImGui::PopItemWidth();

                    // Email
                    ImGui::Text("Email:");
                    ImGui::SameLine(100);
                    ImGui::PushItemWidth(-10);
                    ImGui::InputText("##RegEmail", regEmailBuffer, sizeof(regEmailBuffer));
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    ImGui::Separator();

                    // Check register result from thread
                    if (g_RegisterResultReady) 
                    {
                        g_RegisterResultReady = false;
                        g_PopupMessage = g_RegisterResultMessage;
                        if (g_RegisterSuccess) 
                        {
                            g_PopupMessageType = 1; // Success
                            g_PopupMessage = g_SelecteNgonNgu ? "Registration successful! You can log in immediately." : "Đăng ký thành công! Bạn có thể đăng nhập ngay.";
                            // Copy username/password to login form
                            strcpy(usernameBuffer, regUsernameBuffer);
                            strcpy(passwordBuffer, regPasswordBuffer);
                            CFG.ReplaceKeyValue("username", usernameBuffer);
                            CFG.ReplaceKeyValue("password", passwordBuffer);
                        }
                        else 
                        {
                            g_PopupMessageType = 2; // Error
                        }
                    }

                    // ===== FIXED MESSAGE AREA =====
                    ImGui::BeginChild("##MessageArea", ImVec2(-1, 45), false);
                    if (g_RegisterInProgress.load())
                    {
                        // Loading
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), g_SelecteNgonNgu ? "⏳ Processing..." : "⏳ Đang xử lý...");
                    }
                    else if (!g_PopupMessage.empty())
                    {
                        ImGui::Spacing();
                        if (g_PopupMessageType == 1)
                        {
                            // Success - Green
                            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), u8"✓ %s", g_PopupMessage.c_str());
                        }
                        else if (g_PopupMessageType == 2)
                        {
                            // Error - Red
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), u8"✗ %s", g_PopupMessage.c_str());
                        }
                    }
                    ImGui::EndChild();

                    ImGui::Separator();
                    ImGui::Spacing();

                    // ===== CENTERED BUTTONS (FIXED POSITION) =====
                    float buttonWidth = 120.0f;
                    float buttonSpacing = 20.0f;
                    float totalButtonsWidth = buttonWidth * 2 + buttonSpacing;
                    float windowWidth = ImGui::GetWindowSize().x;
                    float startX = (windowWidth - totalButtonsWidth) * 0.5f;

                    ImGui::SetCursorPosX(startX);

                    bool canRegister = !g_RegisterInProgress.load();
                    if (!canRegister) ImGui::BeginDisabled();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.15f, 1.0f));

                    if (ImGui::Button(g_SelecteNgonNgu ? "Register" : "Đăng ký", ImVec2(buttonWidth, 32)))
                    {
                        std::string regUser = regUsernameBuffer;
                        std::string regPass = regPasswordBuffer;
                        std::string regPassConfirm = regPasswordConfirmBuffer;
                        std::string regEmail = regEmailBuffer;
                        std::string regServer = serverBuffer;

                        bool valid = true;
                        g_PopupMessage = "";
                        g_PopupMessageType = 0;

                        if (regUser.empty()) 
                        {
                            g_PopupMessage = g_SelecteNgonNgu ? "Please enter Account!" : "Vui lòng nhập Tài khoản!";
                            g_PopupMessageType = 2;
                            valid = false;
                        }
                        else if (regUser.length() < 2) 
                        {
                            g_PopupMessage = g_SelecteNgonNgu ? "The account must have at least 2 characters!" : "Tài khoản phải có ít nhất 2 ký tự!";
                            g_PopupMessageType = 2;
                            valid = false;
                        }
                        else if (regPass.empty()) 
                        {
                            g_PopupMessage = g_SelecteNgonNgu ? "Please enter the password!" : "Vui lòng nhập mật khẩu!";
                            g_PopupMessageType = 2;
                            valid = false;
                        }
                        else if (regPass.length() < 6) 
                        {
                            g_PopupMessage = g_SelecteNgonNgu ? "The password must be at least 6 characters long!" : "Mật khẩu phải có ít nhất 6 ký tự!";
                            g_PopupMessageType = 2;
                            valid = false;
                        }
                        else if (regPass != regPassConfirm) 
                        {
                            g_PopupMessage = g_SelecteNgonNgu ? "Confirmation password does not match!" : "Mật khẩu xác nhận không khớp!";
                            g_PopupMessageType = 2;
                            valid = false;
                        }

                        if (valid)
                        {
                            g_RegisterInProgress = true;
                            g_RegisterResultReady = false;
                            g_PopupMessage = "";
                            g_PopupMessageType = 0;

                            std::thread regThread(RegisterThreadFunc, regUser, regPass, regEmail);
                            regThread.detach();
                        }
                    }
                    ImGui::PopStyleColor(3);

                    if (!canRegister) ImGui::EndDisabled();

                    ImGui::SameLine(0, buttonSpacing);

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                    if (ImGui::Button(g_SelecteNgonNgu ? "Close" : "Đóng", ImVec2(buttonWidth, 32)))
                    {
                        showRegisterPopup = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor(3);

                    ImGui::EndPopup();
                }

                ImGui::EndChild();
                ImGui::EndGroup();

                ImGui::SameLine();

                // === RIGHT SIDE: War3 Version ===
                ImGui::BeginGroup();
                ImGui::BeginChild("##RightSettings", ImVec2(rightWidth, 50), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                int previousVersion = selectedWar3Version;

                // War3 Version Combo
                ImGui::Text("War3 Version:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                if (ImGui::BeginCombo("##War3VersionCombo", war3Versions[selectedWar3Version]))
                {
                    for (int i = 0; i < 6; i++)
                    {
                        const bool is_selected = (selectedWar3Version == i);
                        if (ImGui::Selectable(war3Versions[i], is_selected))
                        {
                            selectedWar3Version = i;

                            // Version changed
                            if (selectedWar3Version != previousVersion)
                            {
                                if (nwar3Versions[previousVersion] == 28)
                                {
                                    StopInjectThread();
                                }

                                CFG.ReplaceKeyValue("war3version", to_string(nwar3Versions[selectedWar3Version]));
                                gGProxy->m_War3Version = nwar3Versions[selectedWar3Version];
                                gGProxy->m_BNET->m_War3Version = nwar3Versions[selectedWar3Version];

                                if (gGProxy->m_BNET && gGProxy->m_BNET->m_Socket)
                                {
                                    gGProxy->m_BNET->m_BNCSUtil->SetHashVersion();
                                    gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_CUSTOM_WAR3_VERSION(nwar3Versions[selectedWar3Version], gGProxy->m_BNET->m_BNCSUtil->GetEXEVersion(), gGProxy->m_BNET->m_BNCSUtil->GetEXEVersionHash()));
                                    if (nwar3Versions[selectedWar3Version] == 28)
                                    {
                                        StartInjectThread();
                                    }
                                }

                                CONSOLE_Print(u8"[INFO] Đã chọn War3 version " + string(war3Versions[selectedWar3Version]), dye_light_green);
                            }
                        }

                        // Set focus on selected item when combo opens
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::EndChild();

                // === Bot Name and Host Button ===
                ImGui::Spacing();

                if (ImGui::BeginTabBar("##HostTabs"))
                {
                    if (ImGui::BeginTabItem("Host API"))
                    {
                        if (TabHostAPI == false)
                        {
                            TabHostAPI = true;
                            InitHostAPI();
                            LoadAdminListAsync();
                            LoadBotListAsync();
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(g_SelecteNgonNgu ?
                                "Create rooms through the API system\n"
                                "use a centralized map repository from the server" :
                                "Tạo phòng thông qua hệ thống API\n"
                                "sử dụng kho map tập trung từ máy chủ");
                        }

                        ImGui::BeginChild("##BotAPIControls", ImVec2(rightWidth, 80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                        if (!g_BotListLoading && !botSynced && !g_BotList.empty())
                        {
                            SyncBotFromConfig();
                            botSynced = true;
                        }

                        ImGui::SetNextItemWidth(170.0f);
                        if (ImGui::BeginCombo("##BotCombo", comboPreview.c_str()))
                        {
                            for (size_t i = 0; i < g_BotList.size(); i++)
                            {
                                bool selected = (i == currentBot);

                                BotStatus st = ParseBotStatus(g_BotList[i]);

                                ImVec4 color = (st == BOT_ONLINE) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : (st == BOT_OFFLINE) ? ImVec4(0.6f, 0.6f, 0.6f, 1.0f) : ImVec4(1, 1, 1, 1);

                                std::string name = StripBotStatus(g_BotList[i]);

                                ImGui::PushStyleColor(ImGuiCol_Text, color);

                                if (ImGui::Selectable(name.c_str(), selected))
                                {
                                    currentBot = i;
                                    comboPreview = name;

                                    strcpy_s(botAPINameBuffer, sizeof(botAPINameBuffer), name.c_str());
                                    CFG.ReplaceKeyValue("BotAPIName", botAPINameBuffer);
                                    LoadBotListAsync();
                                }

                                ImGui::PopStyleColor();

                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::SameLine();
                        ImGui::Text("Map:");
                        ImGui::SameLine();
                        ImGui::PushItemWidth(220);
                        if (ImGui::InputText("##BotAPIMap", botAPIMapBuffer, IM_ARRAYSIZE(botAPIMapBuffer), ImGuiInputTextFlags_ReadOnly))
                        {
                            CFG.ReplaceKeyValue("BotAPIMap", botAPIMapBuffer);
                        }
                        ImGui::PopItemWidth();

                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.52f, 0.30f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.82f, 0.62f, 0.38f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.44f, 0.26f, 1.0f));
                        if (AnimatedButton_MirrorSweep("Get", ImVec2(0, 0)))
                        {
                            ClearMessages();
                            showChoseMapPopup = true;
                            ImGui::OpenPopup(g_SelecteNgonNgu ? "Select Map" : "Lựa Chọn Map");
                        }
                        ImGui::PopStyleColor(3);
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(g_SelecteNgonNgu ? "Get the list of Maps" : "Lấy dánh sách Map");
                        }

                        ImGui::Separator();

                        ButtonHost(true, gGProxy->m_War3Version);

                        RenderUploadConfigPopup();

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Host"))
                    {
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(g_SelecteNgonNgu ?
                                "Create a room using a private bot"
                                "host the map directly from that bot" :
                                "Tạo phòng bằng bot riêng\n"
                                "host trực tiếp map của bot đó");
                        }
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

                        ButtonHost(false, gGProxy->m_War3Version);
                        RenderUploadMapLinkPopup();

                        if (TabHostAPI == true)
                        {
                            TabHostAPI = false;
                            g_AdminList.clear();
                            g_BotList.clear();
                        }

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
                ImGui::EndGroup();
            }

            ImGui::EndChild();

            // Channel sidebar
            ImGui::SameLine();
            ImGui::BeginChild("##RightPanel", ImVec2(0, 0), false);
            {
                if (ImGui::BeginChild("##ChannelSidebar", ImVec2(0, -210), true))
                {
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
                }
                ImGui::EndChild();

                if (TabHostAPI == true)
                {
                    if (ImGui::BeginChild("##AdminAPI", ImVec2(0, 190), true))
                    {
                        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "List Admin Host API\nDùng để Upload/Xoá Map");
                        ImGui::SameLine();
                        if (ImGui::Button("Get", ImVec2(0, 0)))
                        {
                            LoadAdminListAsync();
                        }
                        ImGui::Separator();
                        ImGui::Text("Total: %d", (int)g_AdminList.size());
                        ImGui::Separator();

                        float headerHeight = ImGui::GetCursorPosY();

                        float listHeight = 185.0f - headerHeight - 5.0f;

                        if (ImGui::BeginChild("##AdminUserList", ImVec2(0, listHeight), false))
                        {
                            for (const auto& user : g_AdminList)
                            {
                                ImGui::Bullet();
                                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", user.c_str());
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(g_SelecteNgonNgu ? "List Game" : "Danh sách Game"))
        {
            if (ImGui::Button(g_SelecteNgonNgu ? "Refresh Game List" : "Làm mới danh sách Game", ImVec2(180, 35)))
            {
                if (gGProxy && gGProxy->m_BNET)
                {
                    gGProxy->m_BNET->QueueGetGameList(100);
                    gGProxy->m_BNET->m_TotalGames = 0;
                    gGProxy->m_BNET->m_GameList.clear();
                    gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_REQUEST_GAME_LIST());
                }
            }

            ImGui::SameLine();
            ImGui::SetWindowFontScale(1.3f);
            ImGui::Text(g_SelecteNgonNgu ? "Total Games: %d" : "Tổng số Game: %d", gGProxy->m_BNET->m_TotalGames);
            ImGui::SetWindowFontScale(1.0f);

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
                TextCentered(g_SelecteNgonNgu ? "Version" : "Phiên \nbản"); ImGui::NextColumn();
                TextCentered(g_SelecteNgonNgu ? "Game Name" : "Tên Game"); ImGui::NextColumn();
                TextCentered("Bot"); ImGui::NextColumn();
                TextCentered(g_SelecteNgonNgu ? "Name Host" : "Tên Host"); ImGui::NextColumn();
                TextCentered("Map"); ImGui::NextColumn();
                TextCentered("Slots"); ImGui::NextColumn();
                TextCentered(g_SelecteNgonNgu ? "Status" : "Trạng \nThái"); ImGui::NextColumn();
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
                    ImGui::Text("%d / %d", (game.game_status == game_status_open) ? game.current_players - 1 : game.current_players, game.max_players);
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
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), g_SelecteNgonNgu ? "No games found." : "Không tìm thấy Game nào.");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), g_SelecteNgonNgu ? "Click 'Refresh Game List' to load available games." : "Nhấp vào 'Làm mới danh sách Game' để tải các trò chơi có sẵn.");
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ============================================================
        // TAB CÀI ĐẶT (SETUP) - VCREDIST MANAGER
        // ============================================================
        if (ImGui::BeginTabItem("VC++"))
        {
            RenderVCRedistTab();
            ImGui::EndTabItem();
        }

        // ============================================================
        // TAB SETUP - GOOGLE DRIVE DOWNLOADER
        // ============================================================
        /*if (ImGui::BeginTabItem(u8"Setup"))
        {
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(u8"Tải và cài đặt tự động từ Google Drive");

            RenderSetupTab();
            ImGui::EndTabItem();
        }*/

        ImGui::EndTabBar();
    }

    ImGui::End();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 0;

    switch (msg)
    {
    case WM_ACTIVATEAPP:
        if (ImGui::GetCurrentContext() != nullptr)
            ImGui::GetIO().AddFocusEvent(wParam != FALSE);
        break;

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
    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            if (showRegisterPopup)
            {
                showRegisterPopup = false;
                ImGui::CloseCurrentPopup();
                return 0;
            }

            if (CloseUploadPopup())
                return 0;
        }
        break;
    }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static ImFont* LoadMultiLanguageFont(float fontSize = 18.0f)
{
    ImGuiIO& io = ImGui::GetIO();

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
    builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());

    static ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);

    ImFontConfig config;
    config.OversampleH = 1;
    config.OversampleV = 1;

    config.MergeMode = false;
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", fontSize, &config, ranges.Data);
    if (font)
    {
        config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", fontSize, &config, io.Fonts->GetGlyphRangesKorean());
        io.Fonts->Build();
    }

    return font;
}

//#include <GDriveDownloader.h>
//#pragma comment(lib, "gdown.lib")

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

    string versionString = "GProxy++ " + string(GPROXY_VERSION) + " New";

    HWND hwnd = CreateWindowEx(WS_EX_APPWINDOW, wc.lpszClassName, versionString.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 50, 50, 1280, 800, NULL, NULL, wc.hInstance, NULL);
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

    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;

    static const ImWchar ranges[] =
    {
        0x2764, 0x2764, // ❤ heart
        0x2190, 0x21FF, // arrows
        0x2300, 0x23FF, // technical symbols (⏳ )
        0x2600, 0x26FF, // misc symbols (☑ ☀ ⚠)
        0x2700, 0x27BF, // dingbats (✔ ✖)
        0
    };

    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 16.0f, &cfg, ranges);
    io.Fonts->Build();

    if (!g_UnicodeFont)
    {
        g_UnicodeFont = LoadMultiLanguageFont();
        if (!g_UnicodeFont)
        {
            g_UnicodeFont = ImGui::GetIO().Fonts->AddFontDefault();
        }
    }

    /*GDrive::Downloader::Initialize();
    GDrive::Downloader downloader;

    auto result = downloader.DownloadFile("1w1caekSRYWRXpguQH3yaYpNNLW0SABYK");
    if (result.success)
    {
        WriteLog("Success: %s (%d MB)\n", result.filename, result.filesize / (1024.0 * 1024.0));
    }
    else
    {
        WriteLog(result.error.c_str());
    }

    GDrive::Downloader::Cleanup();*/

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