#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

namespace GDrive
{
    // Progress information structure
    struct ProgressInfo
    {
        std::string filename;           // Tên file đang tải
        size_t totalSize;               // Kích thước tổng (bytes)
        size_t downloadedSize;          // Đã tải (bytes)
        int percent;                    // % hoàn thành (0-100)
        double speed;                   // Tốc độ (MB/s)
        double mbDownloaded;            // MB đã tải
        double mbTotal;                 // Tổng MB
        int currentFile;                // File thứ mấy
        int totalFiles;                 // Tổng số file
        bool isActive;                  // Đang tải hay không

        ProgressInfo()
            : totalSize(0)
            , downloadedSize(0)
            , percent(0)
            , speed(0)
            , mbDownloaded(0)
            , mbTotal(0)
            , currentFile(0)
            , totalFiles(0)
            , isActive(false)
        {
        }
    };

    // Callback types
    typedef std::function<void(const ProgressInfo& info)> ProgressCallback;

    typedef std::function<void(bool success, const std::string& filename,
        size_t filesize, double speed,
        const std::string& error)> CompletionCallback;

    // Download result structure
    struct DownloadResult
    {
        bool success;
        std::string filename;
        size_t filesize;
        double speed;
        std::string error;
    };

    // Download options
    struct DownloadOptions
    {
        int maxConcurrent;          // Max concurrent downloads (default: 3)
        int bufferSize;             // Buffer size in bytes (default: 1MB)
        bool useHttp2;              // Use HTTP/2 (default: true)
        bool showProgress;          // Show progress (default: true)
        std::string outputDir;      // Output directory (default: current dir)

        DownloadOptions()
            : maxConcurrent(3)
            , bufferSize(1048576)
            , useHttp2(true)
            , showProgress(true)
            , outputDir(".")
        {
        }
    };

    // Main API functions
    class Downloader
    {
    public:
        Downloader();
        ~Downloader();

        // Initialize CURL (call once at startup)
        static void Initialize();
        static void Cleanup();

        // Extract file ID from various URL formats
        static std::string ExtractFileId(const std::string& urlOrId);

        // Download single file (blocking)
        DownloadResult DownloadFile(const std::string& fileIdOrUrl);
        DownloadResult DownloadFile(const std::string& fileIdOrUrl,
            const DownloadOptions& options);

        // Download multiple files (blocking)
        std::vector<DownloadResult> DownloadFiles(
            const std::vector<std::string>& fileIdsOrUrls);
        std::vector<DownloadResult> DownloadFiles(
            const std::vector<std::string>& fileIdsOrUrls,
            const DownloadOptions& options);

        // Get current progress info (thread-safe)
        ProgressInfo GetProgressInfo() const;

        // Get progress for specific file index (for multi-file downloads)
        ProgressInfo GetProgressInfo(int fileIndex) const;

        // Set callbacks
        void SetProgressCallback(ProgressCallback callback);
        void SetCompletionCallback(CompletionCallback callback);

        // Cancel ongoing downloads
        void Cancel();

        // Check if downloading
        bool IsDownloading() const;

    private:
        class Impl;
        Impl* pImpl;
    };

    // Utility functions
    namespace Utils
    {
        // Load URLs from file
        std::vector<std::string> LoadUrlsFromFile(const std::string& filepath);

        // Format file size
        std::string FormatSize(size_t bytes);

        // Format speed
        std::string FormatSpeed(double mbps);
    }
}