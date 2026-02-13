#ifndef VCREDIST_MANAGER_H
#define VCREDIST_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <windows.h>

struct VCRedistPackage
{
    std::string displayName;
    std::string version;
    std::string arch;          // "x86" or "x64"
    std::string downloadUrl;
    bool isInstalled;
    std::string installedVersion;
    
    VCRedistPackage() : isInstalled(false) {}
    
    VCRedistPackage(const std::string& name, const std::string& ver, const std::string& a, const std::string& url)
        : displayName(name), version(ver), arch(a), downloadUrl(url), isInstalled(false)
    {
    }
};

class VCRedistManager
{
public:
    VCRedistManager();
    ~VCRedistManager();
    
    // Khởi tạo danh sách packages
    void Initialize();
    
    // Kiểm tra trạng thái cài đặt
    void CheckInstalledPackages();
    
    // Cài đặt package
    bool InstallPackage(size_t index);
    
    // Tải về package
    bool DownloadPackage(size_t index, const std::string& savePath);
    
    // Get danh sách packages
    const std::vector<VCRedistPackage>& GetPackages() const { return m_Packages; }
    
    // Kiểm tra một package cụ thể đã cài chưa
    bool IsPackageInstalled(const std::string& displayName);
    
private:
    std::vector<VCRedistPackage> m_Packages;
    
    // Helper functions
    bool CheckRegistryKey(const std::string& keyPath, const std::string& valueName);
    std::string GetInstalledVersion(const std::string& displayName);
    bool IsVCRedistInstalled(const std::string& productCode, const std::string& minVersion);
};

// Render ImGui cho VCRedist tab
void RenderVCRedistTab();

#endif // VCREDIST_MANAGER_H
