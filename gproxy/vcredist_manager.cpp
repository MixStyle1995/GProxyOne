#include "gproxy.h"
#include "gproxy_imgui.h"
#include "vcredist_manager.h"
#include <algorithm>
#include <cctype>

// Global state cho UI
static std::atomic<bool> g_CheckingStatus{ false };
static std::atomic<bool> g_InstallingPackage{ false };
static std::atomic<int> g_InstallingIndex{ -1 };
static std::string g_StatusMessage = "";
// Removed g_StatusMutex - causes deadlock in UI thread
static std::atomic<int> g_DownloadProgress{ 0 };
static std::atomic<bool> g_NeedRefresh{ false };  // Flag to force UI update

// Curl download callback
static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

// Progress callback
static int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    if (dltotal > 0)
    {
        int progress = (int)((dlnow * 100) / dltotal);
        g_DownloadProgress = progress;
    }
    return 0;
}

VCRedistManager::VCRedistManager()
{
}

VCRedistManager::~VCRedistManager()
{
}

void VCRedistManager::Initialize()
{
    m_Packages.clear();

    // =========================================================================
    // VISUAL C++ 2017-2026 (v14) - LATEST (Recommended for modern apps)
    // =========================================================================
    // Latest VC14 channel: Most recent security updates and features
    // Compatible with: VS 2017, 2019, 2022, 2026
    // Use this unless you specifically need VS 2015 support
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2017-2026 (x86) - Latest",
        "14.50.35719",
        "x86",
        "https://aka.ms/vc14/vc_redist.x86.exe"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2017-2026 (x64) - Latest",
        "14.50.35719",
        "x64",
        "https://aka.ms/vc14/vc_redist.x64.exe"
    ));

    // =========================================================================
    // VISUAL C++ 2015-2022 (v14) - LEGACY (For VS 2015 compatibility)
    // =========================================================================
    // VS 2022 Release channel: Older but supports VS 2015
    // Compatible with: VS 2015, 2017, 2019, 2022
    // Use this ONLY if you have apps built with Visual Studio 2015
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2015-2022 (x86) - Legacy",
        "14.44.35211",
        "x86",
        "https://aka.ms/vs/17/release/vc_redist.x86.exe"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2015-2022 (x64) - Legacy",
        "14.44.35211",
        "x64",
        "https://aka.ms/vs/17/release/vc_redist.x64.exe"
    ));

    // =========================================================================
    // VISUAL C++ 2013 (v12.0) - No longer supported
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2013 (x86)",
        "12.0.40664.0",
        "x86",
        "https://aka.ms/highdpimfc2013x86enu"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2013 (x64)",
        "12.0.40664.0",
        "x64",
        "https://aka.ms/highdpimfc2013x64enu"
    ));

    // =========================================================================
    // VISUAL C++ 2012 Update 4 (v11.0) - No longer supported
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2012 Update 4 (x86)",
        "11.0.61030.0",
        "x86",
        "https://download.microsoft.com/download/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU_4/vcredist_x86.exe"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2012 Update 4 (x64)",
        "11.0.61030.0",
        "x64",
        "https://download.microsoft.com/download/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU_4/vcredist_x64.exe"
    ));

    // =========================================================================
    // VISUAL C++ 2010 SP1 (v10.0) - No longer supported
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2010 SP1 (x86)",
        "10.0.40219",
        "x86",
        "https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x86.exe"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2010 SP1 (x64)",
        "10.0.40219",
        "x64",
        "https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x64.exe"
    ));

    // =========================================================================
    // VISUAL C++ 2008 SP1 (v9.0) - No longer supported
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2008 SP1 (x86)",
        "9.0.30729.5677",
        "x86",
        "https://download.microsoft.com/download/5/D/8/5D8C65CB-C849-4025-8E95-C3966CAFD8AE/vcredist_x86.exe"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2008 SP1 (x64)",
        "9.0.30729.5677",
        "x64",
        "https://download.microsoft.com/download/5/D/8/5D8C65CB-C849-4025-8E95-C3966CAFD8AE/vcredist_x64.exe"
    ));

    // =========================================================================
    // VISUAL C++ 2005 SP1 (v8.0) - No longer supported
    // Link requires my.visualstudio.com login, use older direct link
    // =========================================================================
    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2005 SP1 (x86)",
        "8.0.61001",
        "x86",
        "https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x86.EXE"
    ));

    m_Packages.push_back(VCRedistPackage(
        "Visual C++ 2005 SP1 (x64)",
        "8.0.61000",
        "x64",
        "https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x64.EXE"
    ));
}

bool VCRedistManager::CheckRegistryKey(const std::string& keyPath, const std::string& valueName)
{
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey);

    if (result != ERROR_SUCCESS)
    {
        // Try 32-bit registry
        result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ | KEY_WOW64_32KEY, &hKey);
        if (result != ERROR_SUCCESS)
            return false;
    }

    char buffer[512];
    DWORD bufferSize = sizeof(buffer);
    DWORD type;

    result = RegQueryValueExA(hKey, valueName.c_str(), NULL, &type, (LPBYTE)buffer, &bufferSize);
    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS);
}

std::string VCRedistManager::GetInstalledVersion(const std::string& searchKey)
{
    // searchKey = "Visual C++ 2022" (chỉ có year, không có arch)
    // Tìm trong registry
    HKEY hUninstall;
    std::string uninstallKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    std::vector<HKEY> rootKeys = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    std::vector<REGSAM> accessFlags = { KEY_READ | KEY_WOW64_64KEY, KEY_READ | KEY_WOW64_32KEY };

    // Convert search string to lowercase
    std::string searchLower = searchKey;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    for (auto rootKey : rootKeys)
    {
        for (auto access : accessFlags)
        {
            LONG result = RegOpenKeyExA(rootKey, uninstallKey.c_str(), 0, access, &hUninstall);
            if (result != ERROR_SUCCESS)
                continue;

            DWORD index = 0;
            char subKeyName[512];
            DWORD subKeySize = sizeof(subKeyName);

            while (RegEnumKeyExA(hUninstall, index++, subKeyName, &subKeySize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
            {
                HKEY hSubKey;
                if (RegOpenKeyExA(hUninstall, subKeyName, 0, access, &hSubKey) == ERROR_SUCCESS)
                {
                    char name[512] = { 0 };
                    char version[512] = { 0 };
                    DWORD nameSize = sizeof(name);
                    DWORD versionSize = sizeof(version);

                    RegQueryValueExA(hSubKey, "DisplayName", NULL, NULL, (LPBYTE)name, &nameSize);
                    RegQueryValueExA(hSubKey, "DisplayVersion", NULL, NULL, (LPBYTE)version, &versionSize);

                    std::string nameStr = name;
                    std::string nameLower = nameStr;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                    // Simple substring match - if registry name contains search key
                    if (nameLower.find(searchLower) != std::string::npos)
                    {
                        RegCloseKey(hSubKey);
                        RegCloseKey(hUninstall);
                        return version;
                    }

                    RegCloseKey(hSubKey);
                }

                subKeySize = sizeof(subKeyName);
            }

            RegCloseKey(hUninstall);
        }
    }

    return "";
}

// Compare version strings: returns true if ver1 >= ver2
bool CompareVersion(const std::string& ver1, const std::string& ver2)
{
    if (ver1.empty() || ver2.empty())
        return false;

    // Parse versions: "14.32.31326" -> {14, 32, 31326}
    auto parseVersion = [](const std::string& ver) -> std::vector<int> {
        std::vector<int> parts;
        size_t start = 0;
        size_t end = 0;

        while (end != std::string::npos)
        {
            end = ver.find('.', start);
            std::string part = ver.substr(start, end - start);

            // Extract numeric part only
            int num = 0;
            for (char c : part)
            {
                if (c >= '0' && c <= '9')
                    num = num * 10 + (c - '0');
                else
                    break;  // Stop at first non-digit
            }

            parts.push_back(num);
            start = end + 1;
        }

        return parts;
        };

    std::vector<int> v1 = parseVersion(ver1);
    std::vector<int> v2 = parseVersion(ver2);

    // Pad shorter version with zeros
    while (v1.size() < v2.size()) v1.push_back(0);
    while (v2.size() < v1.size()) v2.push_back(0);

    // Compare each part
    for (size_t i = 0; i < v1.size(); i++)
    {
        if (v1[i] > v2[i]) return true;   // v1 is newer
        if (v1[i] < v2[i]) return false;  // v1 is older
    }

    return true;  // Equal versions
}

bool VCRedistManager::IsVCRedistInstalled(const std::string& productCode, const std::string& minVersion)
{
    std::string installedVer = GetInstalledVersion(productCode);
    return !installedVer.empty();
}

void VCRedistManager::CheckInstalledPackages()
{
    for (auto& pkg : m_Packages)
    {
        pkg.isInstalled = false;
        pkg.installedVersion = "";

        // Simple approach: Extract year and arch, search broadly
        std::string year = "";

        // Extract year from package name
        if (pkg.displayName.find("2017-2026") != std::string::npos)
        {
            // For 2017-2026 Latest, try multiple patterns
            std::vector<std::string> searchPatterns = {
                "Visual C++ 2022",           // Most likely (2022 is part of 2017-2026)
                "Visual C++ 2019",
                "Visual C++ 2017",
                "C++ 2022",                  // Shorter pattern
                "C++ 2019",
                "C++ 2017",
                "2022 x86 Additional",       // Runtime variant
                "2022 x64 Additional",
                "2022 x86 Minimum",
                "2022 x64 Minimum",
                "2022 Redistributable"       // Standard redistributable
            };

            for (const auto& pattern : searchPatterns)
            {
                std::string ver = GetInstalledVersion(pattern);
                if (!ver.empty())
                {
                    // Check if installed version meets requirement
                    if (CompareVersion(ver, pkg.version))
                    {
                        // Version is good - show it
                        pkg.installedVersion = ver;
                        pkg.isInstalled = true;
                    }
                    else
                    {
                        // Version too old - don't show, keep required version visible
                        pkg.installedVersion = "";
                        pkg.isInstalled = false;
                    }

                    break;
                }
            }
        }
        else if (pkg.displayName.find("2015-2022") != std::string::npos)
        {
            // For 2015-2022 Legacy, same patterns
            std::vector<std::string> searchPatterns = {
                "Visual C++ 2022",
                "Visual C++ 2019",
                "Visual C++ 2017",
                "Visual C++ 2015",           // Legacy also checks 2015
                "C++ 2022",
                "C++ 2019",
                "C++ 2017",
                "C++ 2015",
                "2022 x86 Additional",
                "2022 x64 Additional",
                "2022 x86 Minimum",
                "2022 x64 Minimum",
                "2022 Redistributable"
            };

            for (const auto& pattern : searchPatterns)
            {
                std::string ver = GetInstalledVersion(pattern);
                if (!ver.empty())
                {
                    // Check version
                    if (CompareVersion(ver, pkg.version))
                    {
                        // Version is good
                        pkg.installedVersion = ver;
                        pkg.isInstalled = true;
                    }
                    else
                    {
                        // Version too old - don't show
                        pkg.installedVersion = "";
                        pkg.isInstalled = false;
                    }

                    break;
                }
            }
        }
        else
        {
            // Extract year: 2013, 2012, 2010, 2008, 2005
            if (pkg.displayName.find("2013") != std::string::npos) year = "2013";
            else if (pkg.displayName.find("2012") != std::string::npos) year = "2012";
            else if (pkg.displayName.find("2010") != std::string::npos) year = "2010";
            else if (pkg.displayName.find("2008") != std::string::npos) year = "2008";
            else if (pkg.displayName.find("2005") != std::string::npos) year = "2005";

            if (!year.empty())
            {
                // Search just by year - registry names vary too much
                std::string searchKey = "Visual C++ " + year;
                std::string ver = GetInstalledVersion(searchKey);
                if (!ver.empty())
                {
                    // Check version
                    if (CompareVersion(ver, pkg.version))
                    {
                        pkg.installedVersion = ver;
                        pkg.isInstalled = true;
                    }
                    else
                    {
                        pkg.installedVersion = "";  // Don't show old version
                        pkg.isInstalled = false;
                    }
                }
            }
        }
    }
}

bool VCRedistManager::DownloadPackage(size_t index, const std::string& savePath)
{
    try
    {
        if (index >= m_Packages.size())
            return false;

        const auto& pkg = m_Packages[index];

        CURL* curl = curl_easy_init();
        if (!curl)
            return false;

        FILE* fp = fopen(savePath.c_str(), "wb");
        if (!fp)
        {
            curl_easy_cleanup(curl);
            return false;
        }

        // Reset progress
        g_DownloadProgress = 0;

        curl_easy_setopt(curl, CURLOPT_URL, pkg.downloadUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "GProxy++/3.4");

        // Add timeouts
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);           // 5 minutes total
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);     // 30 seconds to connect

        // Progress callback
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);

        CURLcode res = curl_easy_perform(curl);

        fclose(fp);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            DeleteFileA(savePath.c_str());

            // Log error for debugging
            //char errBuf[256];
            //sprintf(errBuf, "CURL error: %s", curl_easy_strerror(res));
            //WriteLog(errBuf);

            return false;
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool VCRedistManager::InstallPackage(size_t index)
{
    try
    {
        if (index >= m_Packages.size())
            return false;

        const auto& pkg = m_Packages[index];

        // Get temp path
        char tempPath[4096]{};
        GetTempPathA(4096, tempPath);

        // Use unique filename to avoid conflicts
        char uniqueName[256];
        sprintf(uniqueName, "vcredist_%zu_%lu.exe", index, GetTickCount());
        std::string installer = std::string(tempPath) + uniqueName;

        // Download
        g_StatusMessage = "Đang tải " + pkg.displayName + "...";

        if (!DownloadPackage(index, installer))
        {
            g_StatusMessage = "Lỗi: Không thể tải " + pkg.displayName;
            return false;
        }

        // Verify file exists and size > 0
        HANDLE hFile = CreateFileA(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            g_StatusMessage = "Lỗi: File tải về không tồn tại";
            return false;
        }

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);
        CloseHandle(hFile);

        if (fileSize.QuadPart < 1024) // Less than 1KB = invalid
        {
            g_StatusMessage = "Lỗi: File tải về bị lỗi";
            DeleteFileA(installer.c_str());
            return false;
        }

        // Install
        g_StatusMessage = "Đang cài đặt " + pkg.displayName + "...";

        SHELLEXECUTEINFOA sei = { 0 };
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = "runas";  // Run as admin
        sei.lpFile = installer.c_str();
        sei.lpParameters = "/install /quiet /norestart";
        sei.nShow = SW_HIDE;

        if (!ShellExecuteExA(&sei))
        {
            DWORD dwError = GetLastError();
            char errMsg[256];
            sprintf(errMsg, "Lỗi: Không thể cài đặt (code: %lu)", dwError);
            g_StatusMessage = errMsg;
            DeleteFileA(installer.c_str());
            return false;
        }

        // Wait for installation with TIMEOUT
        if (sei.hProcess)
        {
            DWORD waitResult = WaitForSingleObject(sei.hProcess, 10 * 60 * 1000); // 10 minutes max

            if (waitResult == WAIT_TIMEOUT)
            {
                TerminateProcess(sei.hProcess, 1);
                CloseHandle(sei.hProcess);
                g_StatusMessage = "Lỗi: Cài đặt timeout (>10 phút)";
                DeleteFileA(installer.c_str());
                return false;
            }

            CloseHandle(sei.hProcess);
        }

        DeleteFileA(installer.c_str());

        g_StatusMessage = "Đã cài đặt " + pkg.displayName + " thành công!";

        return true;
    }
    catch (const std::exception& e)
    {
        g_StatusMessage = std::string("Lỗi exception: ") + e.what();
        return false;
    }
    catch (...)
    {
        g_StatusMessage = "Lỗi không xác định trong InstallPackage";
        return false;
    }
}

bool VCRedistManager::IsPackageInstalled(const std::string& displayName)
{
    for (const auto& pkg : m_Packages)
    {
        if (pkg.displayName == displayName)
            return pkg.isInstalled;
    }
    return false;
}

// Global instance
static VCRedistManager* g_VCRedistManager = nullptr;

void RenderVCRedistTab()
{
    static bool g_FirstLoad = true;
    static bool g_WasVisible = false;

    if (!g_VCRedistManager)
    {
        g_VCRedistManager = new VCRedistManager();
        g_VCRedistManager->Initialize();
        g_FirstLoad = true;
    }

    // Detect tab switch - if we just became visible, refresh
    bool isVisible = true;  // This tab is being rendered = visible
    if (isVisible && !g_WasVisible)
    {
        // Just switched TO this tab
        g_FirstLoad = true;
    }
    g_WasVisible = isVisible;

    // Auto-check status on first load or tab switch
    if (g_FirstLoad && !g_CheckingStatus && !g_InstallingPackage)
    {
        g_FirstLoad = false;
        g_CheckingStatus = true;
        std::thread([]() {
            g_VCRedistManager->CheckInstalledPackages();
            g_StatusMessage = "Đã kiểm tra trạng thái";
            g_NeedRefresh = true;  // Force UI refresh
            g_CheckingStatus = false;
            }).detach();
    }

    //ImGui::PushFont(g_UnicodeFont);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 1.0f, 1.0f), "Quản lý Visual C++ Redistributable Packages");
    ImGui::Separator();
    ImGui::Spacing();

    // Buttons
    if (ImGui::Button("Kiểm tra trạng thái", ImVec2(180, 30)))
    {
        if (!g_CheckingStatus)
        {
            g_CheckingStatus = true;
            std::thread([]() {
                g_VCRedistManager->CheckInstalledPackages();
                g_StatusMessage = "Đã kiểm tra xong!";
                g_NeedRefresh = true;  // Force UI refresh
                g_CheckingStatus = false;
                }).detach();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cài đặt tất cả", ImVec2(180, 30)))
    {
        if (!g_InstallingPackage && g_VCRedistManager)
        {
            g_InstallingPackage = true;
            std::thread([]() {
                try
                {
                    if (!g_VCRedistManager)
                    {
                        g_InstallingPackage = false;
                        return;
                    }

                    size_t totalPackages = g_VCRedistManager->GetPackages().size();

                    for (size_t i = 0; i < totalPackages; i++)
                    {
                        if (!g_VCRedistManager)
                            break;

                        // Re-check packages before each install (to get fresh state)
                        const auto& packages = g_VCRedistManager->GetPackages();
                        if (i >= packages.size())
                            break;

                        const auto& pkg = packages[i];
                        if (!pkg.isInstalled)
                        {
                            g_InstallingIndex = (int)i;

                            // Install this package
                            g_VCRedistManager->InstallPackage(i);

                            // Immediately check ALL packages (quick registry scan)
                            // This updates UI immediately for this package
                            if (g_VCRedistManager)
                            {
                                g_VCRedistManager->CheckInstalledPackages();
                            }

                            Sleep(500); // Short wait between installations
                        }
                    }

                    // Re-check status
                    if (g_VCRedistManager)
                    {
                        g_VCRedistManager->CheckInstalledPackages();
                        g_NeedRefresh = true;  // Force UI refresh
                    }

                    g_StatusMessage = "Đã cài đặt tất cả packages!";
                }
                catch (const std::exception& e)
                {
                    char errBuf[512];
                    sprintf(errBuf, "Lỗi: %s", e.what());
                    g_StatusMessage = errBuf;
                }
                catch (...)
                {
                    g_StatusMessage = "Lỗi: Crash trong quá trình cài đặt hàng loạt";
                }

                g_InstallingIndex = -1;
                g_InstallingPackage = false;
                }).detach();
        }
    }

    ImGui::Spacing();

    // Status message
    if (!g_StatusMessage.empty())
    {
        if (g_StatusMessage.find("Lỗi") != std::string::npos)
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", g_StatusMessage.c_str());
        else if (g_StatusMessage.find("thành công") != std::string::npos)
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", g_StatusMessage.c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "%s", g_StatusMessage.c_str());

        // Hiển thị progress bar khi đang download
        if (g_StatusMessage.find("Đang tải") != std::string::npos && g_DownloadProgress > 0)
        {
            char progressText[64];
            sprintf(progressText, "%d%%", g_DownloadProgress.load());
            ImGui::ProgressBar(g_DownloadProgress / 100.0f, ImVec2(-1, 0), progressText);
        }

        ImGui::Spacing();
    }

    // Table
    if (ImGui::BeginTable("VCRedistTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthFixed, 350.0f);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Trạng thái", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Hành động", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        const auto& packages = g_VCRedistManager->GetPackages();
        for (size_t i = 0; i < packages.size(); i++)
        {
            const auto& pkg = packages[i];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Package name
            ImGui::Text("%s", pkg.displayName.c_str());

            ImGui::TableNextColumn();
            // Version - Always show REQUIRED version (what this package provides)
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", pkg.version.c_str());

            // Show tooltip with installed version if different
            if (!pkg.installedVersion.empty() && pkg.installedVersion != pkg.version)
            {
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Version package: %s", pkg.version.c_str());
                    ImGui::Text("Version đã cài: %s", pkg.installedVersion.c_str());
                    ImGui::EndTooltip();
                }
            }

            ImGui::TableNextColumn();
            // Status
            if (pkg.isInstalled)
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "✓ Đã cài đặt");
            }
            else if (!pkg.installedVersion.empty())
            {
                // Has old version
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "⚠ Cần cập nhật");
            }
            else
            {
                // Not installed
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "✗ Chưa cài đặt");
            }

            ImGui::TableNextColumn();
            // Action button
            if (!pkg.isInstalled)
            {
                char btnLabel[64];

                if (!pkg.installedVersion.empty())
                    sprintf(btnLabel, "Cập nhật##%d", (int)i);  // Old version exists
                else
                    sprintf(btnLabel, "Cài đặt##%d", (int)i);   // No version

                bool isInstalling = (g_InstallingPackage && g_InstallingIndex == i);

                if (isInstalling)
                {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Đang cài...");
                }
                else
                {
                    if (ImGui::Button(btnLabel, ImVec2(120, 0)))
                    {
                        if (!g_InstallingPackage)
                        {
                            g_InstallingPackage = true;
                            g_InstallingIndex = i;

                            std::thread([i]() {
                                try
                                {
                                    if (g_VCRedistManager)
                                    {
                                        g_VCRedistManager->InstallPackage(i);
                                        g_VCRedistManager->CheckInstalledPackages();
                                        g_NeedRefresh = true;  // Force UI refresh
                                    }
                                }
                                catch (const std::exception& e)
                                {
                                    char errBuf[512];
                                    sprintf(errBuf, "Lỗi: %s", e.what());
                                    g_StatusMessage = errBuf;
                                }
                                catch (...)
                                {
                                    g_StatusMessage = "Lỗi: Crash trong quá trình cài đặt";
                                }

                                g_InstallingIndex = -1;
                                g_InstallingPackage = false;
                                }).detach();
                        }
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    //ImGui::PopFont();
}