#include "MapUploadAPI.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include "util.h"
#include "Obuscate.hpp"
#include "gproxy_map.h"

string serverurlapi = "http://160.187.146.137/War3API/war3_upload_api.php";
string keycheckadmin = "QGo4NGMl2!wK31cl5V6Y1loOOGY@5Hst";

MapUploadAPI::MapUploadAPI()
    : m_serverUrl(serverurlapi)
    , m_timeout(300) // 5 minutes default
    , m_verbose(false)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

MapUploadAPI::~MapUploadAPI()
{
    curl_global_cleanup();
}

// Write callback cho cURL
size_t MapUploadAPI::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Progress callback cho cURL
int MapUploadAPI::ProgressCallbackInternal(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    UploadContext* ctx = static_cast<UploadContext*>(clientp);

    if (ctx && ctx->callback)
    {
        if (ultotal > 0)
        {
            double percent = (static_cast<double>(ulnow) / static_cast<double>(ultotal)) * 100.0;
            ctx->callback(percent, ulnow, ultotal);
        }
        else if (dltotal > 0)
        {
            double percent = (static_cast<double>(dlnow) / static_cast<double>(dltotal)) * 100.0;
            ctx->callback(percent, dlnow, dltotal);
        }
    }

    try
    {
        if (g_DownloadCancelled.load())
        {
            return 1; // Abort transfer
        }
    }
    catch (...)
    {
        // Ignore if variable doesn't exist
    }


    return 0; // Return 0 to continue
}

// Upload map file
bool MapUploadAPI::UploadMap(const std::string& username, const std::string& filepath, ProgressCallback onProgress, CompletionCallback onComplete)
{
    return PerformUpload(username, filepath, "upload_map", "map", onProgress, onComplete);
}

// Upload config file
bool MapUploadAPI::UploadConfig(const std::string& username, const std::string& filepath, ProgressCallback onProgress, CompletionCallback onComplete)
{
    return PerformUpload(username, filepath, "upload_config", "config", onProgress, onComplete);
}

// Internal upload function
bool MapUploadAPI::PerformUpload(const std::string& username, const std::string& filepath, const std::string& action, const std::string& fieldName, ProgressCallback onProgress, CompletionCallback onComplete)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        if (onComplete)
            onComplete(false, "Failed to initialize cURL", json::object());
        return false;
    }

    bool success = false;
    std::string response;

    XAM_LOL(100);
    std::string hash = ETS_HASH(username + "|" + AdvB64::decode(keycheckadmin));

    // Check if file exists
    std::ifstream file(filepath, std::ios::binary);
    if (!file.good())
    {
        if (onComplete)
            onComplete(false, "File not found: " + filepath, json::object());
        curl_easy_cleanup(curl);
        return false;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    uint64_t fileSize = file.tellg();
    file.close();

    // Tạo form data
    curl_mime* form = curl_mime_init(curl);
    curl_mimepart* field = curl_mime_addpart(form);

    curl_mime_name(field, fieldName.c_str());
    curl_mime_filedata(field, filepath.c_str());

    // Set URL
    std::string url = m_serverUrl + "?action=" + action;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set form data
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    // Set callbacks
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Progress callback
    UploadContext ctx;
    ctx.callback = onProgress;
    ctx.totalSize = fileSize;

    if (onProgress)
    {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallbackInternal);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    // Set timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);

    // Verbose
    if (m_verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-USER: " + username).c_str());
    headers = curl_slist_append(headers, ("X-HASH: " + hash).c_str());

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if (response_code == 200)
        {
            try
            {
                // Parse JSON response
                json jsonData = json::parse(response);

                success = jsonData.contains("success") && jsonData["success"].get<bool>();
                std::string message = jsonData.contains("message") ? jsonData["message"].get<std::string>() : "";

                json data = jsonData.contains("data") ? jsonData["data"] : json::object();

                if (onComplete)
                    onComplete(success, message, data);
            }
            catch (const json::exception& e)
            {
                if (onComplete)
                    onComplete(false, std::string("Failed to parse JSON: ") + e.what(), json::object());
            }
        }
        else
        {
            if (onComplete)
                onComplete(false, "HTTP error: " + std::to_string(response_code), json::object());
        }
    }
    else
    {
        std::string error = curl_easy_strerror(res);
        if (onComplete)
            onComplete(false, "cURL error: " + error, json::object());
    }

    // Cleanup
    curl_mime_free(form);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    XAM_LOL(456);
    return success;
}

// Download map
bool MapUploadAPI::DownloadMap(const std::string& filename, const std::string& savePath, ProgressCallback onProgress)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    bool success = false;
    FILE* fp = fopen(savePath.c_str(), "wb");
    if (!fp)
    {
        curl_easy_cleanup(curl);
        return false;
    }

    std::string url = m_serverUrl + "?action=download_map&file=" + curl_easy_escape(curl, filename.c_str(), 0);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);

    // Progress callback
    UploadContext ctx;
    ctx.callback = onProgress;

    if (onProgress)
    {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallbackInternal);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    if (m_verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        success = (response_code == 200);
    }

    fclose(fp);
    curl_easy_cleanup(curl);

    // Delete file if download failed
    if (!success)
        remove(savePath.c_str());

    return success;
}

// Download config
bool MapUploadAPI::DownloadConfig(const std::string& filename, const std::string& savePath, ProgressCallback onProgress)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    bool success = false;
    FILE* fp = fopen(savePath.c_str(), "wb");
    if (!fp)
    {
        curl_easy_cleanup(curl);
        return false;
    }

    std::string url = m_serverUrl + "?action=download_config&file=" + curl_easy_escape(curl, filename.c_str(), 0);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);

    // Progress callback
    UploadContext ctx;
    ctx.callback = onProgress;

    if (onProgress)
    {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallbackInternal);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    if (m_verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        success = (response_code == 200);
    }

    fclose(fp);
    curl_easy_cleanup(curl);

    if (!success)
        remove(savePath.c_str());

    return success;
}

// Get map list
json MapUploadAPI::GetMapList()
{
    std::string response = PerformGetRequest("list_maps");

    try
    {
        json jsonData = json::parse(response);

        if (jsonData.contains("success") && jsonData["success"].get<bool>())
        {
            if (jsonData.contains("data"))
                return jsonData["data"];
        }
    }
    catch (const json::exception&)
    {
        // Return empty array on error
    }

    return json::array();
}

// Get config list
json MapUploadAPI::GetConfigList()
{
    std::string response = PerformGetRequest("list_configs");

    try
    {
        json jsonData = json::parse(response);

        if (jsonData.contains("success") && jsonData["success"].get<bool>())
        {
            if (jsonData.contains("data"))
                return jsonData["data"];
        }
    }
    catch (const json::exception&)
    {
        // Return empty array on error
    }

    return json::array();
}

// Get admin list
json MapUploadAPI::GetAdminList()
{
    std::string response = PerformGetRequest("list_admin");

    try
    {
        json jsonData = json::parse(response);

        if (jsonData.contains("success") && jsonData["success"].get<bool>())
        {
            if (jsonData.contains("data") && jsonData["data"].contains("admins") && jsonData["data"]["admins"].is_array())
                return jsonData["data"]["admins"];
        }
    }
    catch (const json::exception&)
    {
        // Return empty array on error
    }

    return json::array();

}

// Get bot list
json MapUploadAPI::GetBotList()
{
    std::string response = PerformGetRequest("list_bot");

    try
    {
        json jsonData = json::parse(response);

        if (jsonData.contains("success") && jsonData["success"].get<bool>())
        {
            if (jsonData.contains("data") && jsonData["data"].contains("bots") && jsonData["data"]["bots"].is_array())
                return jsonData["data"]["bots"];
        }
    }
    catch (const json::exception&)
    {
        // Return empty array on error
    }

    return json::array();

}

// Delete map
bool MapUploadAPI::DeleteMap(const std::string& username, const std::string& filename)
{
    return PerformDeleteRequest(username, "delete_map", filename);
}

// Delete config
bool MapUploadAPI::DeleteConfig(const std::string& username, const std::string& filename)
{
    return PerformDeleteRequest(username, "delete_config", filename);
}

// Check if map exists
bool MapUploadAPI::MapExists(const std::string& mapName)
{
    json maps = GetMapList();

    if (maps.is_array())
    {
        for (const auto& map : maps)
        {
            if (map.contains("filename"))
            {
                std::string filename = map["filename"].get<std::string>();
                if (filename.find(mapName) != std::string::npos)
                    return true;
            }
        }
    }

    return false;
}

// Get map info
json MapUploadAPI::GetMapInfo(const std::string& filename)
{
    json maps = GetMapList();

    if (maps.is_array())
    {
        for (const auto& map : maps)
        {
            if (map.contains("filename") && map["filename"].get<std::string>() == filename)
                return map;
        }
    }

    return json::object();
}

// Perform GET request
std::string MapUploadAPI::PerformGetRequest(const std::string& action)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    std::string url = m_serverUrl + "?action=" + action;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);

    if (m_verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return response;
}

// Perform DELETE request
bool MapUploadAPI::PerformDeleteRequest(const std::string& username, const std::string& action, const std::string& filename)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    bool success = false;
    std::string response;
    std::string url = m_serverUrl + "?action=" + action;
    std::string postData = "filename=" + filename;

    XAM_LOL(789);
    std::string hash = ETS_HASH(username + "|" + AdvB64::decode(keycheckadmin));

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);

    if (m_verbose)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-USER: " + username).c_str());
    headers = curl_slist_append(headers, ("X-HASH: " + hash).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        try
        {
            json jsonData = json::parse(response);
            success = jsonData.contains("success") && jsonData["success"].get<bool>();
        }
        catch (const json::exception&)
        {
            // Return false on parse error
        }
    }

    curl_easy_cleanup(curl);
    XAM_LOL(1111);
    return success;
}