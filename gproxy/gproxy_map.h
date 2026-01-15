#pragma once

#include <string>
#include <atomic>

// Global popup state
extern bool showUploadConfigPopup;
extern bool showChoseMapPopup;
extern std::atomic<bool> g_DownloadCancelled;

// Functions
void RenderUploadConfigPopup();
bool CloseUploadPopup();
void InitMapAPI();
void LoadMapListAsync();
void ClearMessages();

void InitializeWar3Path();
void RenderWar3PathPopup();
bool StartMapDownload(const std::string& filename, uint64_t mapSize, int mapIndex);