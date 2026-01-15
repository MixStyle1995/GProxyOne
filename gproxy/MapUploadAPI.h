#ifndef MAP_UPLOAD_API_H
#define MAP_UPLOAD_API_H

#include "Include.h"

// Callback cho progress
typedef std::function<void(double percent, uint64_t downloaded, uint64_t total)> ProgressCallback;

// Callback cho completion
typedef std::function<void(bool success, const std::string& message, const json& data)> CompletionCallback;

class MapUploadAPI
{
public:
    MapUploadAPI();
    ~MapUploadAPI();

    // Upload map file
    bool UploadMap(const std::string& username, const std::string& filepath,  ProgressCallback onProgress = nullptr, CompletionCallback onComplete = nullptr);

    // Upload config file
    bool UploadConfig(const std::string& username, const std::string& filepath, ProgressCallback onProgress = nullptr, CompletionCallback onComplete = nullptr);

    // Download map từ server
    bool DownloadMap(const std::string& filename, const std::string& savePath, ProgressCallback onProgress = nullptr);

    // Download config từ server
    bool DownloadConfig(const std::string& filename, const std::string& savePath, ProgressCallback onProgress = nullptr);

    // Lấy danh sách maps
    json GetMapList();

    // Lấy danh sách configs
    json GetConfigList();

    // Lấy danh sách admin
    json GetAdminList();

    // Lấy danh sách bot
    json GetBotList();

    // Xóa map
    bool DeleteMap(const std::string& username, const std::string& filename);

    // Xóa config
    bool DeleteConfig(const std::string& username, const std::string& filename);

    // Check map tồn tại trên server
    bool MapExists(const std::string& mapName);

    // Get map info
    json GetMapInfo(const std::string& filename);

    // Set timeout
    void SetTimeout(long seconds) { m_timeout = seconds; }

    // Enable/disable verbose
    void SetVerbose(bool verbose) { m_verbose = verbose; }

private:
    std::string m_serverUrl;
    long m_timeout;
    bool m_verbose;

    // Helper functions
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static int ProgressCallbackInternal(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    
    bool PerformUpload(const std::string& username, const std::string& filepath, const std::string& action, const std::string& fieldName, ProgressCallback onProgress, CompletionCallback onComplete);

    std::string PerformGetRequest(const std::string& action);
    bool PerformDeleteRequest(const std::string& username, const std::string& action, const std::string& filename);
};

// Upload context for progress tracking
struct UploadContext
{
    ProgressCallback callback;
    uint64_t totalSize;
};

#endif // MAP_UPLOAD_API_H
