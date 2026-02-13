#pragma once

#include <string>
#include <atomic>

// Global popup state
extern bool showUploadConfigPopup;
extern bool showChoseMapPopup;
extern std::atomic<bool> g_DownloadCancelled;
extern char g_mappassword[256];
extern bool g_ShowPasswordTextUpload;

// Functions
void RenderUploadConfigPopup();
bool CloseUploadPopup();
void InitMapAPI();
void LoadMapListAsync();
void ClearMessages();

void InitializeWar3Path();
void RenderWar3PathPopup();
void RenderInputTextMapPassword();

bool StartMapDownload(const std::string& filename, uint64_t mapSize, int mapIndex);
void StartMapDownloadWithPassword(const std::string& filename, uint64_t mapSize, int mapIndex, const std::string& password);