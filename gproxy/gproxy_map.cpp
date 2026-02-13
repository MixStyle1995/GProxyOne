// Copy toàn bộ phần CRC32, SHA1, helper functions từ file cũ (dòng 1-481)
// Paste vào đây...

// Sau đó thêm phần UI mới:

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <algorithm>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx9.h>

#include "gproxy.h"
#include "gproxy_imgui.h"
#include "gproxy_map.h"
#include "MapUploadAPI.h"
#include "crc32.h"
#include "sha1.h"
#include "resource1.h"
#include "Obuscate.hpp"
#include "Include.h"

using namespace std;

#pragma pack(1)
// ============================================================================
// Slot Structure
// ============================================================================
struct GameSlot 
{
    uint8_t PID;
    uint8_t DownloadStatus;
    uint8_t SlotStatus;
    uint8_t Computer;
    uint8_t Team;
    uint8_t Colour;
    uint8_t Race;
    uint8_t ComputerType;
    uint8_t Handicap;
};

// ============================================================================
// Map Config Structure
// ============================================================================
struct MapConfig 
{
    std::string filename;
    std::vector<uint8_t> mapData;
    std::vector<uint8_t> mapSize;      // 4 bytes
    std::vector<uint8_t> mapInfo;      // 4 bytes (CRC)
    std::vector<uint8_t> mapCRC;       // 4 bytes (xoro)
    std::vector<uint8_t> mapSHA1;      // 20 bytes
    std::vector<uint8_t> mapHash;      // 20 bytes (file hash)
    std::vector<uint8_t> mapWidth;     // 2 bytes
    std::vector<uint8_t> mapHeight;    // 2 bytes
    uint32_t mapOptions;
    uint32_t mapNumPlayers;
    uint32_t mapNumTeams;
    uint8_t  mapFilterType;
    uint8_t  map_LANWar3Version;
    bool     mapIsLua;
    std::vector<GameSlot> slots;
};
#pragma pack()

std::string g_MapCfgPath;
std::string g_CommonJ;
std::string g_BlizzardJ;

// Global variables
bool showUploadConfigPopup = false;
bool showChoseMapPopup = false;
static MapUploadAPI* g_MapAPI = nullptr;

// Processed map config
static MapConfig g_ProcessedMapConfig;
static bool g_MapProcessed = false;
static std::string g_ConfigFilePath;

// Upload state  
static std::atomic<bool> g_UploadInProgress{ false };
static std::atomic<double> g_UploadProgress{ 0.0 };
static std::string g_UploadMessage;
static int g_UploadMessageType = 0;

// UI state
static char g_SelectedMapPath[512] = "";
static int g_SelectedWar3Version = 3; // Default 1.28
static bool g_UploadMapChecked = true;
static bool g_UploadConfigChecked = true;
static bool g_UploadLockDow = false;

// Map list
static std::vector<json> g_MapList;
static std::mutex g_MapListMutex;
static std::atomic<bool> g_MapListLoading{ false };
static char g_SearchBuffer[256] = "";
static std::vector<json> g_FilteredMapList;

static std::atomic<bool> g_DownloadInProgress{ false };
static std::atomic<double> g_DownloadProgress{ 0.0 };
static std::atomic<uint64_t> g_DownloadCurrent{ 0 };
static std::atomic<uint64_t> g_DownloadTotal{ 0 };
static std::string g_DownloadingFilename;
static int g_DownloadingMapIndex = -1;
static std::mutex g_DownloadMutex;
std::atomic<bool> g_DownloadCancelled{ false };

// War3 path state
static std::string g_War3Path;
static std::string g_War3MapsPath;
static bool g_ShowWar3PathPopup = false;
static char g_War3PathBuffer[4096] = "";
char g_mappassword[256] = "";
static bool g_ShowPasswordDialog = false;
static char g_DownloadPasswordBuffer[256] = "";
static std::string g_PendingDownloadFilename;
static uint64_t g_PendingDownloadSize = 0;
static int g_PendingDownloadIndex = -1;
static bool g_ShowPasswordText = false;
bool g_ShowPasswordTextUpload = false;

static bool checknamemap = true;

struct ColoredTextSegment
{
    std::string text;
    ImVec4 color;
    bool hasColor;
};

std::vector<ColoredTextSegment> ParseWar3ColoredText(const std::string& input)
{
    std::vector<ColoredTextSegment> segments;

    size_t pos = 0;
    size_t len = input.length();

    while (pos < len)
    {
        // Tìm color code |c
        size_t colorPos = input.find("|c", pos);

        if (colorPos == std::string::npos)
        {
            // Không còn color code, thêm text còn lại
            std::string remaining = input.substr(pos);

            // Remove |r
            size_t resetPos;
            while ((resetPos = remaining.find("|r")) != std::string::npos)
            {
                remaining.erase(resetPos, 2);
            }

            if (!remaining.empty())
            {
                ColoredTextSegment seg;
                seg.text = remaining;
                seg.hasColor = false;
                seg.color = ImVec4(1, 1, 1, 1); // Default white
                segments.push_back(seg);
            }
            break;
        }

        // Thêm text trước color code
        if (colorPos > pos)
        {
            std::string plainText = input.substr(pos, colorPos - pos);

            // Remove |r
            size_t resetPos;
            while ((resetPos = plainText.find("|r")) != std::string::npos)
            {
                plainText.erase(resetPos, 2);
            }

            if (!plainText.empty())
            {
                ColoredTextSegment seg;
                seg.text = plainText;
                seg.hasColor = false;
                seg.color = ImVec4(1, 1, 1, 1);
                segments.push_back(seg);
            }
        }

        // Parse color code (|cffRRGGBB = 10 chars)
        if (colorPos + 10 <= len)
        {
            // Extract hex color (skip |c, get 8 hex chars)
            std::string hexColor = input.substr(colorPos + 2, 8);

            // Convert AARRGGBB to RGB (skip first 2 chars - alpha)
            std::string rgbHex = hexColor.substr(2, 6);

            // Parse RGB
            unsigned int rgb;
            sscanf(rgbHex.c_str(), "%x", &rgb);

            float r = ((rgb >> 16) & 0xFF) / 255.0f;
            float g = ((rgb >> 8) & 0xFF) / 255.0f;
            float b = (rgb & 0xFF) / 255.0f;

            // Tìm end của colored text (|r hoặc end of string)
            size_t endPos = input.find("|r", colorPos + 10);
            if (endPos == std::string::npos)
            {
                endPos = len;
            }

            // Get colored text
            std::string coloredText = input.substr(colorPos + 10, endPos - (colorPos + 10));

            if (!coloredText.empty())
            {
                ColoredTextSegment seg;
                seg.text = coloredText;
                seg.hasColor = true;
                seg.color = ImVec4(r, g, b, 1.0f);
                segments.push_back(seg);
            }

            // Move position past |r if exists
            if (endPos < len && input.substr(endPos, 2) == "|r")
            {
                pos = endPos + 2;
            }
            else
            {
                pos = endPos;
            }
        }
        else
        {
            // Invalid color code, skip
            pos = colorPos + 1;
        }
    }

    return segments;
}

// Render War3 colored text trong ImGui
void RenderWar3ColoredText(const std::string& text)
{
    auto segments = ParseWar3ColoredText(text);

    if (segments.empty())
    {
        if (g_UnicodeFont)
            ImGui::PushFont(g_UnicodeFont);

        ImGui::Text("%s", text.c_str());

        if (g_UnicodeFont)
            ImGui::PopFont();
        return;
    }

    if (g_UnicodeFont)
        ImGui::PushFont(g_UnicodeFont);

    for (size_t i = 0; i < segments.size(); i++)
    {
        const auto& seg = segments[i];

        if (seg.hasColor)
        {
            ImGui::TextColored(seg.color, "%s", seg.text.c_str());
        }
        else
        {
            ImGui::Text("%s", seg.text.c_str());
        }

        // SameLine nếu không phải segment cuối
        if (i < segments.size() - 1)
        {
            ImGui::SameLine(0, 0); // No spacing
        }
    }

    if (g_UnicodeFont)
        ImGui::PopFont();
}

std::string StripWar3ColorCodes(const std::string& text)
{
    std::string result = text;

    // Remove |cffRRGGBB
    size_t pos;
    while ((pos = result.find("|c")) != std::string::npos)
    {
        if (pos + 10 <= result.length())
        {
            result.erase(pos, 10);
        }
        else
        {
            break;
        }
    }

    // Remove |r
    while ((pos = result.find("|r")) != std::string::npos)
    {
        result.erase(pos, 2);
    }

    return result;
}

#define MAPSPEED_SLOW 1
#define MAPSPEED_NORMAL 2
#define MAPSPEED_FAST 3

#define MAPVIS_HIDETERRAIN 1
#define MAPVIS_EXPLORED 2
#define MAPVIS_ALWAYSVISIBLE 3
#define MAPVIS_DEFAULT 4

#define MAPOBS_NONE 1
#define MAPOBS_ONDEFEAT 2
#define MAPOBS_ALLOWED 3
#define MAPOBS_REFEREES 4

#define MAPFLAG_TEAMSTOGETHER 1
#define MAPFLAG_FIXEDTEAMS 2
#define MAPFLAG_UNITSHARE 4
#define MAPFLAG_RANDOMHERO 8
#define MAPFLAG_RANDOMRACES 16

#define MAPOPT_HIDEMINIMAP 1 << 0
#define MAPOPT_MODIFYALLYPRIORITIES 1 << 1
#define MAPOPT_MELEE 1 << 2 // the bot cares about this one...
#define MAPOPT_REVEALTERRAIN 1 << 4
#define MAPOPT_FIXEDPLAYERSETTINGS 1 << 5 // and this one...
#define MAPOPT_CUSTOMFORCES 1 << 6        // and this one, the rest don't affect the bot's logic
#define MAPOPT_CUSTOMTECHTREE 1 << 7
#define MAPOPT_CUSTOMABILITIES 1 << 8
#define MAPOPT_CUSTOMUPGRADES 1 << 9
#define MAPOPT_WATERWAVESONCLIFFSHORES 1 << 11
#define MAPOPT_WATERWAVESONSLOPESHORES 1 << 12

#define MAPFILTER_MAKER_USER 1
#define MAPFILTER_MAKER_BLIZZARD 2

#define MAPFILTER_TYPE_MELEE 1
#define MAPFILTER_TYPE_SCENARIO 2

#define MAPFILTER_SIZE_SMALL 1
#define MAPFILTER_SIZE_MEDIUM 2
#define MAPFILTER_SIZE_LARGE 4

#define MAPFILTER_OBS_FULL 1
#define MAPFILTER_OBS_ONDEATH 2
#define MAPFILTER_OBS_NONE 4

#define MAPGAMETYPE_UNKNOWN0 1 // always set except for saved games?
#define MAPGAMETYPE_BLIZZARD 1 << 3
#define MAPGAMETYPE_MELEE 1 << 5
#define MAPGAMETYPE_SAVEDGAME 1 << 9
#define MAPGAMETYPE_PRIVATEGAME 1 << 11
#define MAPGAMETYPE_MAKERUSER 1 << 13
#define MAPGAMETYPE_MAKERBLIZZARD 1 << 14
#define MAPGAMETYPE_TYPEMELEE 1 << 15
#define MAPGAMETYPE_TYPESCENARIO 1 << 16
#define MAPGAMETYPE_SIZESMALL 1 << 17
#define MAPGAMETYPE_SIZEMEDIUM 1 << 18
#define MAPGAMETYPE_SIZELARGE 1 << 19
#define MAPGAMETYPE_OBSFULL 1 << 20
#define MAPGAMETYPE_OBSONDEATH 1 << 21
#define MAPGAMETYPE_OBSNONE 1 << 22

#define SLOTSTATUS_OPEN 0
#define SLOTSTATUS_CLOSED 1
#define SLOTSTATUS_OCCUPIED 2

#define SLOTRACE_HUMAN 1
#define SLOTRACE_ORC 2
#define SLOTRACE_NIGHTELF 4
#define SLOTRACE_UNDEAD 8
#define SLOTRACE_RANDOM 32
#define SLOTRACE_SELECTABLE 64

#define SLOTCOMP_EASY 0
#define SLOTCOMP_NORMAL 1
#define SLOTCOMP_HARD 2

string RemoveSpecificExtension(const string& filename)
{
    if (filename.size() >= 4)
    {
        string ext = filename.substr(filename.size() - 4);
        if (ext == ".w3x" || ext == ".w3m")
        {
            return filename.substr(0, filename.size() - 4);
        }
    }
    return filename;
}

static CCRC32 g_CRC32;
static CSHA1 g_SHA1;

// ============================================================================
// Helper Functions
// ============================================================================

inline std::vector<uint8_t> CreateByteArray(const uint8_t* a, const int32_t size)
{
    if (size < 1)
        return std::vector<uint8_t>();

    return std::vector<uint8_t>(a, a + size);
}

inline std::vector<uint8_t> CreateByteArray(const uint8_t c)
{
    return std::vector<uint8_t>{c};
}

inline std::vector<uint8_t> CreateByteArray(const uint16_t i, bool reverse)
{
    if (!reverse)
        return std::vector<uint8_t>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)};
    else
        return std::vector<uint8_t>{static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
}

inline std::vector<uint8_t> CreateByteArray(const uint32_t i, bool reverse)
{
    if (!reverse)
        return std::vector<uint8_t>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)};
    else
        return std::vector<uint8_t>{static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
}

inline std::vector<uint8_t> CreateByteArray(const int64_t i, bool reverse)
{
    if (!reverse)
        return std::vector<uint8_t>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)};
    else
        return std::vector<uint8_t>{static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
}

inline std::string ByteArrayToDecString(const std::vector<uint8_t>& b)
{
    if (b.empty())
        return std::string();

    std::string result = std::to_string(b[0]);

    for (auto i = cbegin(b) + 1; i != cend(b); ++i)
        result += " " + std::to_string(*i);

    return result;
}

#define ROTL(x, n) ((x) << (n)) | ((x) >> (32 - (n))) // this won't work with signed types
#define ROTR(x, n) ((x) >> (n)) | ((x) << (32 - (n))) // this won't work with signed types

uint32_t XORRotateLeft(uint8_t* data, uint32_t length)
{
    // a big thank you to Strilanc for figuring this out

    uint32_t i = 0;
    uint32_t Val = 0;

    if (length > 3)
    {
        while (i < length - 3)
        {
            Val = ROTL(Val ^ ((uint32_t)data[i] + (uint32_t)(data[i + 1] << 8) + (uint32_t)(data[i + 2] << 16) + (uint32_t)(data[i + 3] << 24)), 3);
            i += 4;
        }
    }

    while (i < length)
    {
        Val = ROTL(Val ^ data[i], 3);
        ++i;
    }

    return Val;
}

uint32_t ChunkedChecksum(unsigned char* data, int32_t length, uint32_t checksum)
{
    int32_t index = 0;
    int32_t t = length - 0x400;
    while (index <= t)
    {
        checksum = ROTL(checksum ^ XORRotateLeft(&data[index], 0x400), 3);
        index += 0x400;
    }
    return checksum;
}

std::string ReadFileToString(const std::string& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
        return {};

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void CConfig_ReplaceKeyValue(std::string strFile, const std::string& key, const std::string& value)
{
    std::ifstream in(strFile);
    std::string out, line;
    bool found = false;
    while (getline(in, line)) {
        if (line.rfind(key + " =", 0) == 0) {
            out += key + " = " + value + "\n";
            found = true;
        }
        else out += line + "\n";
    }
    if (!found) out += key + " = " + value + "\n";
    std::ofstream(strFile) << out;
}

bool AutoDecryptMPQ(unsigned char* buffer, size_t size)
{
   
    return true;
}

// ============================================================================
// LOAD AND PROCESS MAP
// ============================================================================
bool LoadAndProcessMap(const std::string& filepath, const std::string& filename, const std::string& szPath, MapConfig& entry, int nWar3Version)
{
    int versions[] = { 24, 26, 27, 28, 29, 31 };

    entry.filename = filename;
    entry.mapIsLua = false;
    entry.map_LANWar3Version = versions[nWar3Version];

    // Read file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    size_t fileSize = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);

    entry.mapData.resize(fileSize);
    file.read(reinterpret_cast<char*>(entry.mapData.data()), fileSize);
    file.close();

    AutoDecryptMPQ(entry.mapData.data(), entry.mapData.size());
    
    bool mpqReady = false;
    HANDLE hMPQ = nullptr;
    
    if (SFileOpenArchiveEx(entry.mapData.data(), (DWORD)entry.mapData.size(), 0, MPQ_OPEN_FORCE_MPQ_V1, &hMPQ))
        mpqReady = true;

    // Default values
    entry.mapWidth = CreateByteArray((uint16_t)256, false);
    entry.mapHeight = CreateByteArray((uint16_t)256, false);
    entry.mapOptions = 0;
    entry.mapNumPlayers = 12;
    entry.mapNumTeams = 2;
    entry.mapFilterType = 2; // SCENARIO
    entry.slots.clear();

    // Parse war3map.w3i
    if (mpqReady) 
    {
        //ParseW3IFromMPQ(hMPQ, entry);

        HANDLE hFile = nullptr;
        if (SFileOpenFileEx(hMPQ, "war3map.w3i", 0, &hFile)) 
        {
            DWORD fileLen = SFileGetFileSize(hFile, nullptr);
            if (fileLen > 0 && fileLen != 0xFFFFFFFF) 
            {
                std::vector<char> w3iData(fileLen);
                DWORD bytesRead = 0;
                if (SFileReadFile(hFile, w3iData.data(), fileLen, &bytesRead, nullptr)) 
                {
                    std::istringstream iss(std::string(w3iData.data(), bytesRead));

                    uint32_t fileFormat;
                    iss.read(reinterpret_cast<char*>(&fileFormat), 4);

                    if (fileFormat == 18 || fileFormat == 25 || fileFormat == 28 || fileFormat == 31) 
                    {
                        std::string garbage;

                        iss.seekg(4, std::ios::cur); // saves
                        if (fileFormat >= 28) {
                            iss.seekg(16, std::ios::cur);
                            entry.map_LANWar3Version = 31;
                        }
                        iss.seekg(4, std::ios::cur); // editor version
                        std::getline(iss, garbage, '\0'); // name
                        std::getline(iss, garbage, '\0'); // author
                        std::getline(iss, garbage, '\0'); // description
                        std::getline(iss, garbage, '\0'); // recommended
                        iss.seekg(32, std::ios::cur); // camera bounds
                        iss.seekg(16, std::ios::cur); // camera bounds complements

                        uint32_t width, height, flags;
                        iss.read(reinterpret_cast<char*>(&width), 4);
                        iss.read(reinterpret_cast<char*>(&height), 4);
                        iss.read(reinterpret_cast<char*>(&flags), 4);

                        entry.mapWidth = CreateByteArray(static_cast<uint16_t>(width), false);
                        entry.mapHeight = CreateByteArray(static_cast<uint16_t>(height), false);
                        entry.mapOptions = flags & (MAPOPT_MELEE | MAPOPT_FIXEDPLAYERSETTINGS | MAPOPT_CUSTOMFORCES);

                        iss.seekg(1, std::ios::cur); // ground type

                        if (fileFormat >= 25) {
                            iss.seekg(4, std::ios::cur);
                            std::getline(iss, garbage, '\0');
                        }
                        else {
                            iss.seekg(4, std::ios::cur);
                        }

                        std::getline(iss, garbage, '\0');
                        std::getline(iss, garbage, '\0');
                        std::getline(iss, garbage, '\0');

                        if (fileFormat >= 25) {
                            iss.seekg(4, std::ios::cur);
                            std::getline(iss, garbage, '\0');
                        }
                        else {
                            iss.seekg(4, std::ios::cur);
                        }

                        std::getline(iss, garbage, '\0');
                        std::getline(iss, garbage, '\0');
                        std::getline(iss, garbage, '\0');

                        uint32_t scriptLang = 0;
                        if (fileFormat >= 25) {
                            iss.seekg(4 + 4 + 4 + 4 + 1 + 1 + 1 + 1 + 4, std::ios::cur);
                            std::getline(iss, garbage, '\0');
                            iss.seekg(5, std::ios::cur);

                            if (fileFormat >= 28) {
                                iss.read(reinterpret_cast<char*>(&scriptLang), 4);
                            }
                            if (fileFormat >= 31) {
                                iss.seekg(8, std::ios::cur);
                            }
                        }
                        entry.mapIsLua = (scriptLang > 0);

                        // Read players
                        uint32_t numPlayers;
                        iss.read(reinterpret_cast<char*>(&numPlayers), 4);

                        uint32_t ClosedSlots = 0;
                        for (uint32_t i = 0; i < numPlayers; i++)
                        {
                            GameSlot slot = { 0, 255, SLOTSTATUS_CLOSED, 0, 0, 1, SLOTRACE_RANDOM, SLOTCOMP_NORMAL, 100 };

                            uint32_t colour, status, race;
                            iss.read(reinterpret_cast<char*>(&colour), 4); // colour
                            slot.Colour = colour;
                            iss.read(reinterpret_cast<char*>(&status), 4); // status

                            if (status == 1) slot.SlotStatus = SLOTSTATUS_OPEN;
                            else if (status == 2) {
                                slot.SlotStatus = SLOTSTATUS_OCCUPIED;
                                slot.Computer = 1;
                                slot.ComputerType = SLOTCOMP_NORMAL;
                            }
                            else
                            {
                                slot.SlotStatus = SLOTSTATUS_CLOSED;
                                ++ClosedSlots;
                            }

                            iss.read(reinterpret_cast<char*>(&race), 4);
                            if (race == 1) slot.Race = SLOTRACE_HUMAN;
                            else if (race == 2) slot.Race = SLOTRACE_ORC;
                            else if (race == 3) slot.Race = SLOTRACE_UNDEAD;
                            else if (race == 4) slot.Race = SLOTRACE_NIGHTELF;
                            else slot.Race = SLOTRACE_RANDOM;

                            iss.seekg(4, std::ios::cur); // fixed start position
                            std::getline(iss, garbage, '\0'); // player name
                            iss.seekg(4, std::ios::cur); // start pos x
                            iss.seekg(4, std::ios::cur); // start pos y
                            iss.seekg(4, std::ios::cur); // ally low
                            iss.seekg(4, std::ios::cur); // ally high
                            if (fileFormat >= 31)
                            {
                                iss.seekg(4, std::ios::cur); // enemy low priorities
                                iss.seekg(4, std::ios::cur); // enemy high priorities
                            }

                            if (slot.SlotStatus != SLOTSTATUS_CLOSED) {
                                slot.Team = i % 2;
                                entry.slots.push_back(slot);
                            }
                        }

                        entry.mapNumPlayers = numPlayers - ClosedSlots;

                        // Read teams
                        uint32_t numTeams;
                        iss.read(reinterpret_cast<char*>(&numTeams), 4);

                        for (uint32_t i = 0; i < numTeams; ++i)
                        {
                            uint32_t PlayerMask;

                            if (i < numTeams)
                            {
                                iss.seekg(4, ios::cur);                            // flags
                                iss.read(reinterpret_cast<char*>(&PlayerMask), 4); // player mask
                            }

                            if (!(flags & MAPOPT_CUSTOMFORCES))
                            {
                                PlayerMask = 1 << i;
                            }

                            for (auto& Slot : entry.slots)
                            {
                                if (0 != (PlayerMask & (1 << static_cast<uint32_t>((Slot).Colour))))
                                {

                                    Slot.Team = static_cast<uint8_t>(i);
                                }
                            }

                            if (i < numTeams)
                            {
                                getline(iss, garbage, '\0'); // team name
                            }
                        }
                    }
                }
            }
            SFileCloseFile(hFile);
        }
    }

    // Calculate map_crc (xoro) and map_sha1
    g_SHA1.Reset();

    entry.mapSize = CreateByteArray(static_cast<uint32_t>(entry.mapData.size()), false);
    entry.mapInfo = CreateByteArray(g_CRC32.CalculateCRC((uint8_t*)entry.mapData.data(), entry.mapData.size()), false);

    uint32_t Val = 0;

    bool OverrodeCommonJ = false;
    bool OverrodeBlizzardJ = false;

    if (mpqReady)
    {
        HANDLE SubFile;

        // override common.j

        if (SFileOpenFileEx(hMPQ, R"(Scripts\common.j)", 0, &SubFile))
        {
            uint32_t FileLength = SFileGetFileSize(SubFile, nullptr);

            if (FileLength > 0 && FileLength != 0xFFFFFFFF)
            {
                auto  SubFileData = new char[FileLength];
                DWORD BytesRead = 0;

                if (SFileReadFile(SubFile, SubFileData, FileLength, &BytesRead, nullptr))
                {
                    OverrodeCommonJ = true;
                    Val = Val ^ XORRotateLeft(reinterpret_cast<uint8_t*>(SubFileData), BytesRead);
                    g_SHA1.Update(reinterpret_cast<uint8_t*>(SubFileData), BytesRead);
                }

                delete[] SubFileData;
            }

            SFileCloseFile(SubFile);
        }
    }

    if (!OverrodeCommonJ)
    {
        Val = Val ^ XORRotateLeft((uint8_t*)g_CommonJ.data(), g_CommonJ.size());
        g_SHA1.Update((uint8_t*)g_CommonJ.data(), g_CommonJ.size());
    }

    if (mpqReady)
    {
        HANDLE SubFile;

        // override blizzard.j

        if (SFileOpenFileEx(hMPQ, R"(Scripts\blizzard.j)", 0, &SubFile))
        {
            uint32_t FileLength = SFileGetFileSize(SubFile, nullptr);

            if (FileLength > 0 && FileLength != 0xFFFFFFFF)
            {
                auto  SubFileData = new char[FileLength];
                DWORD BytesRead = 0;

                if (SFileReadFile(SubFile, SubFileData, FileLength, &BytesRead, nullptr))
                {
                    OverrodeBlizzardJ = true;
                    Val = Val ^ XORRotateLeft(reinterpret_cast<uint8_t*>(SubFileData), BytesRead);
                    g_SHA1.Update(reinterpret_cast<uint8_t*>(SubFileData), BytesRead);
                }

                delete[] SubFileData;
            }

            SFileCloseFile(SubFile);
        }
    }

    if (!OverrodeBlizzardJ)
    {
        Val = Val ^ XORRotateLeft((uint8_t*)g_BlizzardJ.data(), g_BlizzardJ.size());
        g_SHA1.Update((uint8_t*)g_BlizzardJ.data(), g_BlizzardJ.size());
    }

    Val = ROTL(Val, 3);
    Val = ROTL(Val ^ 0x03F1379E, 3);
    g_SHA1.Update((uint8_t*)"\x9E\x37\xF1\x03", 4);

    // MPQ files for checksum
    if (mpqReady)
    {
        std::vector<std::string> fileList;
        if (entry.mapIsLua) 
        {
            fileList.push_back("war3map.lua");
            fileList.push_back("scripts\\war3map.lua");
        }
        else 
        {
            fileList.push_back("war3map.j");
            fileList.push_back("scripts\\war3map.j");
        }
        fileList.push_back("war3map.w3e");
        fileList.push_back("war3map.wpm");
        fileList.push_back("war3map.doo");
        fileList.push_back("war3map.w3u");
        fileList.push_back("war3map.w3b");
        fileList.push_back("war3map.w3d");
        fileList.push_back("war3map.w3a");
        fileList.push_back("war3map.w3q");

        bool FoundScript = false;

        for (const auto& fileName : fileList) 
        {
            if (FoundScript && (fileName == "scripts\\war3map.j" || fileName == "scripts\\war3map.lua"))
                continue;

            HANDLE SubFile;
            if (SFileOpenFileEx(hMPQ, fileName.c_str(), 0, &SubFile))
            {
                DWORD FileLength = SFileGetFileSize(SubFile, nullptr);
                if (FileLength > 0 && FileLength != 0xFFFFFFFF)
                {
                    auto  SubFileData = new char[FileLength];
                    DWORD BytesRead = 0;

                    if (SFileReadFile(SubFile, SubFileData, FileLength, &BytesRead, nullptr))
                    {
                        if (entry.map_LANWar3Version >= 32)
                        {
                            if (FoundScript)
                                if (entry.map_LANWar3Version >= 33)
                                    Val = ROTL(Val ^ XORRotateLeft((unsigned char*)SubFileData, BytesRead), 3);
                                else
                                    Val = ChunkedChecksum((unsigned char*)SubFileData, BytesRead, Val);
                            else
                                Val = XORRotateLeft((unsigned char*)SubFileData, BytesRead);
                        }
                        else
                        Val = ROTL(Val ^ XORRotateLeft((unsigned char*)SubFileData, BytesRead), 3);
                        if (fileName == "war3map.j" || fileName == "war3map.lua" || fileName == "scripts\\war3map.j" || fileName == "scripts\\war3map.lua")
                            FoundScript = true;

                        g_SHA1.Update((unsigned char*)SubFileData, BytesRead);
                    }

                    delete[] SubFileData;
                }
                SFileCloseFile(SubFile);
            }
        }
    }

    entry.mapCRC = CreateByteArray(Val, false);

    g_SHA1.Final();
    uint8_t sha1[20] = {};
    memset(sha1, 0, sizeof(uint8_t) * 20);
    g_SHA1.GetHash(sha1);
    entry.mapSHA1 = CreateByteArray(sha1, 20);

    // Calculate map_hash (file hash)
    g_SHA1.Reset();
    g_SHA1.Update((uint8_t*)entry.mapData.data(), entry.mapData.size());
    g_SHA1.Final();
    uint8_t hash[20] = {};
    memset(hash, 0, sizeof(uint8_t) * 20);
    g_SHA1.GetHash(hash);
    entry.mapHash = CreateByteArray(hash, 20);

    if (mpqReady)
    {
        SFileCloseArchive(hMPQ);
    }

    g_ConfigFilePath = g_MapCfgPath + "/" + RemoveSpecificExtension(entry.filename) + ".cfg";

    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_path", std::string("maps\\") + entry.filename);
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_type", std::string());
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_localpath", entry.filename);
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_islua", entry.mapIsLua ? "1" : "0");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_LANWar3Version", to_string(entry.map_LANWar3Version));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_size", ByteArrayToDecString(entry.mapSize));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_info", ByteArrayToDecString(entry.mapInfo));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_crc", ByteArrayToDecString(entry.mapCRC));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_sha1", ByteArrayToDecString(entry.mapSHA1));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_hash", ByteArrayToDecString(entry.mapHash));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_filter_type", to_string(entry.mapFilterType));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_speed", "3");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_visibility", "4");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_observers", "1");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_flags", "3");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_filter_maker", "1");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_filter_size", "4");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_filter_obs", "4");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_options", to_string(entry.mapOptions));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_width", ByteArrayToDecString(entry.mapWidth));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_height", ByteArrayToDecString(entry.mapHeight));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_defaulthcl", string());
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_numplayers", to_string(entry.mapNumPlayers));
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_numteams", to_string(entry.mapNumTeams));

    for (size_t i = 0; i < entry.slots.size(); i++)
    {
        const auto& slot = entry.slots[i];
        CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_slot" + to_string(i + 1),
            to_string((int)slot.PID) + " " +
            to_string((int)slot.DownloadStatus) + " " +
            to_string((int)slot.SlotStatus) + " " +
            to_string((int)slot.Computer) + " " +
            to_string((int)slot.Team) + " " +
            to_string((int)slot.Colour) + " " +
            to_string((int)slot.Race) + " " +
            to_string((int)slot.ComputerType) + " " +
            to_string((int)slot.Handicap));
    }

    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_cfgonly", "1");
    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_lockdow", g_UploadLockDow ? "1" : "0");

    if (strlen(g_mappassword) > 0)
    {
        std::string encodedPassword = AdvB64::encode(std::string(g_mappassword));
        CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_pass", encodedPassword);
        memset(g_mappassword, 0, sizeof(g_mappassword));
    }

    /*std::string cfgContent = ReadFileToString(cfgPath);
    if (cfgContent.empty())
        return false;*/

    return true;
}

// Helper to format bytes
std::string FormatBytes(uint64_t bytes)
{
    const char* units[] = { "B", "KB", "MB", "GB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }

    char buffer[64];
    sprintf_s(buffer, "%.2f %s", size, units[unitIndex]);
    return std::string(buffer);
}

// Initialize API
void InitMapAPI()
{
    if (!g_MapAPI)
        g_MapAPI = new MapUploadAPI();
}

// Load map list
void LoadMapListAsync()
{
    if (g_MapListLoading) return;
    g_MapListLoading = true;

    std::thread([]() 
    {
        InitMapAPI();
        json mapList = g_MapAPI->GetMapList();
        {
            std::lock_guard<std::mutex> lock(g_MapListMutex);
            g_MapList.clear();

            if (mapList.is_array()) 
            {
                for (const auto& map : mapList)
                {
                    g_MapList.push_back(map);
                }
            }
        }

        g_MapListLoading = false;
    }).detach();
}

// Filter map list
void FilterMapList()
{
    std::lock_guard<std::mutex> lock(g_MapListMutex);
    g_FilteredMapList.clear();

    std::string searchTerm = g_SearchBuffer;
    std::transform(searchTerm.begin(), searchTerm.end(), searchTerm.begin(), ::tolower);

    for (const auto& map : g_MapList) 
    {
        if (searchTerm.empty()) 
        {
            g_FilteredMapList.push_back(map);
        }
        else 
        {
            std::string filename = map.value("map_name", "");
            std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);

            if (filename.find(searchTerm) != std::string::npos) 
            {
                g_FilteredMapList.push_back(map);
            }
        }
    }
}

void ProcessMapThread(const std::string& filepath, const std::string& filename, const std::string& szPath, int nWar3Version)
{
	g_MapCfgPath = szPath;

    g_UploadMessage = g_SelecteNgonNgu ? "Processing map file..." : "Đang xử lý Map...";
    g_UploadMessageType = 3;

    // Initialize CRC32
    g_CRC32.Initialize();

    if (nWar3Version == 29)
    {
        g_CommonJ = LoadFileFromMPQ("common-129.j");
        g_BlizzardJ = LoadFileFromMPQ("blizzard-129.j");
    }
    else if (nWar3Version >= 30)
    {
        g_CommonJ = LoadFileFromMPQ("common-131.j");
        g_BlizzardJ = LoadFileFromMPQ("blizzard-131.j");
    }
    else
    {
        g_CommonJ = LoadFileFromMPQ("common.j");
        g_BlizzardJ = LoadFileFromMPQ("blizzard.j");
    }

    if (g_CommonJ.empty() || g_BlizzardJ.empty())
    {
        g_UploadMessage = "Failed Read common.j and blizzard.j";
        g_UploadMessageType = 2;
        g_MapProcessed = false;
        return;
    }

    MapConfig config;
    if (LoadAndProcessMap(filepath, filename, szPath, config, nWar3Version))
    {
        g_ProcessedMapConfig = config;
        g_MapProcessed = true;

        g_UploadMessage = g_SelecteNgonNgu ? "Map processed successfully! Ready to upload." : "Map đã được xử lý thành công! Sẵn sàng để tải lên.";
        g_UploadMessageType = 1;
    }
    else 
    {
        g_UploadMessage = g_SelecteNgonNgu ? "Failed to process map file!" : "Quá trình xử lý Map thất bại!";
        g_UploadMessageType = 2;
        g_MapProcessed = false;
    }
}

// Upload thread
void UploadThread(const std::string& mapPath, const std::string& configPath, bool uploadMap, bool uploadConfig)
{
    InitMapAPI();

    bool mapSuccess = true;
    bool configSuccess = true;

    auto progressCallback = [](double percent, uint64_t downloaded, uint64_t total) 
    {
        g_UploadProgress = percent;
    };

    // Upload map
    if (uploadMap) 
    {
        g_UploadMessage = g_SelecteNgonNgu ? "Uploading map..." : "Đang tải Map lên...";
        g_UploadMessageType = 3;

        auto mapCompleteCallback = [&mapSuccess](bool success, const std::string& message, const json& data) 
        {
            mapSuccess = success;
            if (!success) 
            {
                g_UploadMessage = g_SelecteNgonNgu ? ("Map upload failed: " + message) : ("Tải Map lên không thành công: " + message);
                g_UploadMessageType = 2;
            }
        };

        g_MapAPI->UploadMap(GGetUserName(), mapPath, progressCallback, mapCompleteCallback);
    }

    // Upload config
    if (uploadConfig && configSuccess && mapSuccess)
    {
        g_UploadMessage = g_SelecteNgonNgu ? "Uploading config..." : "Đang tải cấu hình Map lên...";
        g_UploadMessageType = 3;
        g_UploadProgress = 0.0;

        auto configCompleteCallback = [&configSuccess](bool success, const std::string& message, const json& data) 
        {
            configSuccess = success;
            if (!success) 
            {
                g_UploadMessage = g_SelecteNgonNgu ? ("Config upload failed: " + message) : ("Tải cấu hình Map lên không thành công: " + message);
                g_UploadMessageType = 2;
            }
        };

        g_MapAPI->UploadConfig(GGetUserName(), configPath, progressCallback, configCompleteCallback);
    }

    // Final message
    if (mapSuccess && configSuccess) 
    {
        if (uploadMap && uploadConfig) 
        {
            g_UploadMessage = g_SelecteNgonNgu ? "Map and config uploaded successfully!" : "Map và cấu hình đã được tải lên thành công!";
            DeleteFileA(configPath.c_str());
        }
        else if (uploadMap) 
        {
            g_UploadMessage = g_SelecteNgonNgu ? "Map uploaded successfully!" : "Map đã được tải lên thành công!";
        }
        else if (uploadConfig) 
        {
            g_UploadMessage = g_SelecteNgonNgu ? "Config uploaded successfully!" : "Cấu hình Map đã được tải lên thành công!";
			DeleteFileA(configPath.c_str());
        }
        g_UploadMessageType = 1;
        LoadMapListAsync();
    }

    g_UploadInProgress = false;
    g_UploadProgress = 0.0;
}

void RenderStatus()
{
    ImGui::BeginChild("##MessageArea1", ImVec2(0, 45), true);
    ImGui::Text(g_SelecteNgonNgu ? "Status: " : "Trạng thái: ");
    if (!g_UploadMessage.empty())
    {
        ImGui::SameLine();
        if (g_UploadMessageType == 1)
        {
            // Success - Green
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), u8"✓ %s", g_UploadMessage.c_str());
        }
        else if (g_UploadMessageType == 2)
        {
            // Error - Red
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), u8"✗ %s", g_UploadMessage.c_str());
        }
        else if (g_UploadMessageType == 3)
        {
            // Info - Yellow
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), u8"⚠ %s", g_UploadMessage.c_str());
        }
    }
    ImGui::EndChild();
}

void RenderDownloadPasswordDialog()
{
    if (g_ShowPasswordDialog)
    {
        ImGui::OpenPopup(g_SelecteNgonNgu ? "Enter Password" : "Nhập mật khẩu");
        g_ShowPasswordDialog = false;
        g_ShowPasswordText = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 200));

    bool isOpen = true;
    if (!ImGui::BeginPopupModal(g_SelecteNgonNgu ? "Enter Password" : "Nhập mật khẩu",&isOpen, ImGuiWindowFlags_NoResize))
        return;

    ImGui::Text(g_SelecteNgonNgu ? "Map: %s" : "Map: %s", g_PendingDownloadFilename.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text(g_SelecteNgonNgu ? "Password:" : "Mật khẩu:");
    ImGui::SameLine();
    ImGui::PushItemWidth(250);
    if (g_ShowPasswordText)
    {
        ImGui::InputText("##DownloadPassword", g_DownloadPasswordBuffer, sizeof(g_DownloadPasswordBuffer));
    }
    else
    {
        ImGui::InputText("##DownloadPassword", g_DownloadPasswordBuffer, sizeof(g_DownloadPasswordBuffer), ImGuiInputTextFlags_Password);
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
    if (ImGui::Button(g_ShowPasswordText ? "(o)" : "(-)", ImVec2(40, 0)))
    {
        g_ShowPasswordText = !g_ShowPasswordText;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(g_ShowPasswordText ? (g_SelecteNgonNgu ? "Hide password" : "Ẩn mật khẩu") : (g_SelecteNgonNgu ? "Show password" : "Hiện mật khẩu"));
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = 100.0f;
    float startX = (ImGui::GetWindowWidth() - btnW * 2 - 10) * 0.5f;
    ImGui::SetCursorPosX(startX);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.40f, 0.65f, 1.00f));
    if (ImGui::Button("OK", ImVec2(btnW, 30)))
    {
        std::string password = g_DownloadPasswordBuffer;

        if (password.empty())
        {
            g_UploadMessage = g_SelecteNgonNgu ? "Please enter password!" : "Vui lòng nhập mật khẩu!";
            g_UploadMessageType = 2;
        }
        else
        {
            std::string encodedPassword = AdvB64::encode(password);
            StartMapDownloadWithPassword(g_PendingDownloadFilename, g_PendingDownloadSize, g_PendingDownloadIndex, encodedPassword);
            memset(g_DownloadPasswordBuffer, 0, sizeof(g_DownloadPasswordBuffer));
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    if (ImGui::Button(g_SelecteNgonNgu ? "Cancel" : "Huỷ", ImVec2(btnW, 30)))
    {
        memset(g_DownloadPasswordBuffer, 0, sizeof(g_DownloadPasswordBuffer));
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);

    ImGui::EndPopup();
}

void RenderInputTextMapPassword()
{
    ImGui::Spacing();
    ImGui::Text(g_SelecteNgonNgu ? "Map Password (optional):" : "Mật khẩu Map (tùy chọn):");
    ImGui::SameLine();
    ImGui::PushItemWidth(250);

    if (g_ShowPasswordTextUpload)
        ImGui::InputText("##MapPassword", g_mappassword, sizeof(g_mappassword));
    else
        ImGui::InputText("##MapPassword", g_mappassword, sizeof(g_mappassword), ImGuiInputTextFlags_Password);

    ImGui::PopItemWidth();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
    if (ImGui::Button(g_ShowPasswordTextUpload ? "(o)" : "(-)", ImVec2(40, 0)))
    {
        g_ShowPasswordTextUpload = !g_ShowPasswordTextUpload;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(g_ShowPasswordTextUpload ? (g_SelecteNgonNgu ? "Hide password" : "Ẩn mật khẩu") : (g_SelecteNgonNgu ? "Show password" : "Hiện mật khẩu"));
    }
    ImGui::PopStyleColor(3);

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), g_SelecteNgonNgu ? "Leave empty if no password needed" : "Để trống nếu không cần mật khẩu");
    ImGui::Spacing();
}

// Render upload popup
void RenderUploadConfigPopup()
{
    static bool initialized = false;
    if (!initialized) 
    {
        InitMapAPI();
        LoadMapListAsync();
        InitializeWar3Path();
        initialized = true;
    }

    /*if (showUploadConfigPopup) 
    {
        ImGui::OpenPopup("Upload Map/Config");
        showUploadConfigPopup = false;
    }

    if (showChoseMapPopup)
    {
        ImGui::OpenPopup("Lựa Chọn Map");
        showChoseMapPopup = false;
    }*/
    
    ImGui::SetNextWindowSize(ImVec2(620, 600));

    if (ImGui::BeginPopupModal("Upload Map/Config", &showUploadConfigPopup, ImGuiWindowFlags_NoResize))
    {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), g_SelecteNgonNgu ? "Warcraft III Map Upload Manager" : "Trình Quản Lý Tải Lên Map Warcraft III");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();

        // =========================
        // MAP SELECTION & PROCESSING
        // =========================
        ImGui::BeginChild("MapSelection", ImVec2(0, 370), true);
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), g_SelecteNgonNgu ? "Map File Selection" : "Chọn Map");
            ImGui::Spacing();

            // File selection
            ImGui::Text(g_SelecteNgonNgu ? "Selected Map:" : "Map đã chọn:");
            ImGui::SameLine();
            std::string filename;
            std::string dirPath;
            if (strlen(g_SelectedMapPath) > 0) 
            {
                std::string path = g_SelectedMapPath;
                size_t lastSlash = path.find_last_of("\\/");
                filename = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
                dirPath = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : "";
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", path.c_str());
            }
            else 
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), g_SelecteNgonNgu ? "None" : "Không có");
            }

            ImGui::Spacing();

            // Browse button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.7f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));

            if (ImGui::Button(g_SelecteNgonNgu ? "Browse Map File" : "Duyệt Map", ImVec2(150, 35)))
            {
                OPENFILENAMEA ofn;
                char tempPath[512] = "";

                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = NULL;
                ofn.lpstrFile = tempPath;
                ofn.nMaxFile = sizeof(tempPath);
                ofn.lpstrFilter = "Warcraft III Maps\0*.w3x;*.w3m\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = NULL;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileNameA(&ofn)) 
                {
                    strcpy_s(g_SelectedMapPath, tempPath);
                    g_MapProcessed = false; // Reset processed state
                }
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();

            // War3 version selector
            ImGui::Text(g_SelecteNgonNgu ? "War3 Version:" : "Phiên bản War3:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            const char* versions[] = { "1.24", "1.26", "1.27", "1.28", "1.29", "1.31" };
            ImGui::Combo("##War3Version", &g_SelectedWar3Version, versions, IM_ARRAYSIZE(versions));

            ImGui::SameLine();

            // Process button
            bool canProcess = strlen(g_SelectedMapPath) > 0 && !g_UploadInProgress;
            if (!canProcess) ImGui::BeginDisabled();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.5f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.3f, 0.1f, 1.0f));

            if (ImGui::Button(g_SelecteNgonNgu ? "Process Map" : "Xử lý Map", ImVec2(150, 35)))
            {
                std::thread(ProcessMapThread, std::string(g_SelectedMapPath), filename, dirPath, g_SelectedWar3Version).detach();
            }

            ImGui::PopStyleColor(3);
            if (!canProcess) ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Map info display
            if (g_MapProcessed)
            {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), g_SelecteNgonNgu ? u8"✓ Map Processed Successfully" : u8"✓ Đã xử lý Map thành công");
                ImGui::Spacing();

                ImGui::Columns(2, "MapProcessInfo", false);
                ImGui::SetColumnWidth(0, 200);

                ImGui::Text(g_SelecteNgonNgu ? "Filename:" : "Tên Map");
                ImGui::NextColumn();
                ImGui::Text("%s", g_ProcessedMapConfig.filename.c_str());
                ImGui::NextColumn();

                ImGui::Text(g_SelecteNgonNgu ? "Size:" : "Kích thước:");
                ImGui::NextColumn();
                uint32_t mapSize = 0;
                if (g_ProcessedMapConfig.mapSize.size() >= 4) 
                {
                    mapSize = g_ProcessedMapConfig.mapSize[0] | (g_ProcessedMapConfig.mapSize[1] << 8) | (g_ProcessedMapConfig.mapSize[2] << 16) | (g_ProcessedMapConfig.mapSize[3] << 24);
                }
                ImGui::Text("%s", FormatBytes(mapSize).c_str());
                ImGui::NextColumn();

                ImGui::Text(g_SelecteNgonNgu ? "War3 Version:" : "Phiên bản War3:");
                ImGui::NextColumn();
                ImGui::Text("1.%d", g_ProcessedMapConfig.map_LANWar3Version);
                ImGui::NextColumn();

                ImGui::Columns(1);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), g_SelecteNgonNgu ? "No map processed yet." : "Chưa có Map nào được xử lý.");
                ImGui::TextWrapped(g_SelecteNgonNgu ? "Select a map file and click 'Process Map' to generate config file." : "Chọn một Map và nhấp vào 'Xử lý Map' để tạo tệp cấu hình.");
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Spacing();
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), g_SelecteNgonNgu ? "Select what to upload:" : "Chọn nội dung để tải lên:");
            ImGui::Spacing();

            ImGui::Checkbox(g_SelecteNgonNgu ? "Upload Map File (.w3x)" : "Tải Map lên (.w3x)", &g_UploadMapChecked);
            ImGui::SameLine();
            ImGui::Checkbox(g_SelecteNgonNgu ? "Upload Config File (.cfg)" : "Tải cấu hình Map lên (.cfg)", &g_UploadConfigChecked);

            ImGui::Checkbox(g_SelecteNgonNgu ? "Lock Download Map" : "Khoá tải map về", &g_UploadLockDow);

            RenderInputTextMapPassword();
        }
        ImGui::EndChild();

        ImGui::Spacing();
        RenderStatus();
        ImGui::Spacing();

        // Upload button
        bool canUpload = g_MapProcessed && !g_UploadInProgress && (g_UploadMapChecked || g_UploadConfigChecked);

        // Progress bar
        ImGui::Spacing();
        float progress = static_cast<float>(g_UploadProgress.load()) / 100.0f;
        char progressText[64] = {};
        sprintf_s(progressText, "%.1f%%", g_UploadProgress.load());
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressText);

        if (!canUpload) ImGui::BeginDisabled();

        float buttonWidths = 200.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidths) * 0.5f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.6f, 0.25f, 1.0f));

        const char* strupload = g_SelecteNgonNgu ? "Uploading..." : "Đang tải lên";
        const char* strupload2 = g_SelecteNgonNgu ? "Start Upload" : "Bắt đầu tải lên";
        if (ImGui::Button(g_UploadInProgress ? strupload : strupload2, ImVec2(buttonWidths, 40)))
        {
            if (!g_ConfigFilePath.empty())
            {
                CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_lockdow", g_UploadLockDow ? "1" : "0");

                if (strlen(g_mappassword) > 0)
                {
                    std::string encodedPassword = AdvB64::encode(std::string(g_mappassword));
                    CConfig_ReplaceKeyValue(g_ConfigFilePath, "map_pass", encodedPassword);
                    memset(g_mappassword, 0, sizeof(g_mappassword));
                }
            }

            g_UploadInProgress = true;
            std::thread(UploadThread, std::string(g_SelectedMapPath), g_ConfigFilePath, g_UploadMapChecked, g_UploadConfigChecked).detach();
        }

        ImGui::PopStyleColor(3);
        if (!canUpload) ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(1000, 700));

    if (ImGui::BeginPopupModal(g_SelecteNgonNgu ? "Select Map" : "Lựa Chọn Map", &showChoseMapPopup, ImGuiWindowFlags_NoResize))
    {
        // =========================
        // SERVER MAPS
        // =========================

        // Search bar
        ImGui::BeginChild("SearchBar", ImVec2(0, 55), true);
        {
            ImGui::Text(g_SelecteNgonNgu ? "Search:" : "Tìm Kiếm");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(500);
            if (ImGui::InputText("##Search", g_SearchBuffer, sizeof(g_SearchBuffer)))
            {
                FilterMapList();
            }

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.7f, 1.0f));
            if (ImGui::Button(g_MapListLoading ? "Loading..." : (g_SelecteNgonNgu ? "Refresh" : "Làm mới"), ImVec2(100, 25)))
            {
                LoadMapListAsync();
            }
            ImGui::PopStyleColor(3);

            // Nút Set War3 Path
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.3f, 1.0f));
            if (ImGui::Button("Set War3 Path", ImVec2(120, 25)))
            {
                // Load path hiện tại vào buffer
                if (!g_War3Path.empty())
                    strcpy(g_War3PathBuffer, g_War3Path.c_str());
                else
                    strcpy(g_War3PathBuffer, "");

                g_ShowWar3PathPopup = true;  // Hiển thị popup
            }
            ImGui::PopStyleColor(3);

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text(u8"Chọn/Thay đổi đường dẫn Warcraft III");
                if (!g_War3Path.empty())
                {
                    ImGui::Separator();
                    ImGui::Text(u8"Hiện tại: %s", g_War3Path.c_str());
                }
                ImGui::EndTooltip();
            }

            ImGui::SameLine();
			ImGui::Checkbox(u8"Hiển thị tên Map", &checknamemap);
        }
        ImGui::EndChild();

        ImGui::Spacing();
        RenderStatus();
        ImGui::Spacing();
        // Map list
        ImGui::BeginChild("MapList", ImVec2(0, 400), true);
        {
            if (g_MapListLoading)
            {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Loading map list...");
            }
            else
            {
                FilterMapList();

                if (g_FilteredMapList.empty())
                {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No maps found.");
                }
                else
                {
                    if (ImGui::BeginTable("MapsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
                    {
                        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("Filename", ImGuiTableColumnFlags_WidthFixed, 300.0f);
                        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("Upload Time", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 270.0f);
                        ImGui::TableHeadersRow();

                        // Render rows
                        for (size_t i = 0; i < g_FilteredMapList.size(); i++)
                        {
                            const auto& map = g_FilteredMapList[i];

                            ImGui::TableNextRow();
                            bool isRowHovered = false;

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%d", i + 1);
                            isRowHovered |= ImGui::IsItemHovered();

                            ImGui::TableSetColumnIndex(1);
                            std::string filename = map.value("filename", "Unknown");
                            std::string mapname = map.value("map_name", "Unknown");
                            RenderWar3ColoredText(checknamemap ? mapname : filename);
                            isRowHovered |= ImGui::IsItemHovered();

                            ImGui::TableSetColumnIndex(2);
                            uint64_t size = map.value("size", 0);
                            ImGui::Text("%s", FormatBytes(size).c_str());
                            isRowHovered |= ImGui::IsItemHovered();

                            ImGui::TableSetColumnIndex(3);
                            std::string uploadTime = map.value("upload_time", "Unknown");
                            ImGui::Text("%s", uploadTime.c_str());
                            isRowHovered |= ImGui::IsItemHovered();

                            ImGui::TableSetColumnIndex(4);

                            ImGui::PushID(i);

                            auto ChoseMapAction = [&]()
                            {
                                strcpy(botAPIMapBuffer, filename.c_str());
                                CFG.ReplaceKeyValue("BotAPIMap", botAPIMapBuffer);
                                ImGui::CloseCurrentPopup();
                                g_UploadMessage = "";
                                g_UploadMessageType = 0;
							};

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.45f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.55f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.40f, 1.00f));
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
                            if (ImGui::Button(g_SelecteNgonNgu ? "Select" : "Chọn", ImVec2(80, 20)))
                            {
                                ChoseMapAction();
                            }
                            ImGui::PopStyleColor(3);
                            ImGui::PopStyleVar(2);
                            isRowHovered |= ImGui::IsItemHovered();

                            ImGui::SameLine();

                            if (isRowHovered)
                            {
                                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(140, 140, 0, 255));
                                ImGui::BeginTooltip();
                                RenderWar3ColoredText(checknamemap ? filename : mapname);
                                ImGui::EndTooltip();

                                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                                {
                                    ChoseMapAction();
                                }
                            }

                            // Kiểm tra xem map này có đang download không
                            bool isThisMapDownloading = (g_DownloadInProgress && g_DownloadingMapIndex == static_cast<int>(i));
                            bool isAnyMapDownloading = g_DownloadInProgress.load();

                            if (isThisMapDownloading)
                            {
                                // Hiển thị progress bar thay cho button Download
                                double progress = g_DownloadProgress.load();
                                uint64_t current = g_DownloadCurrent.load();
                                uint64_t total = g_DownloadTotal.load();

                                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

                                ImGui::ProgressBar(
                                    static_cast<float>(progress / 100.0),
                                    ImVec2(80, 20),
                                    ""
                                );

                                ImGui::PopStyleColor(2);

                                // Tooltip khi hover
                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Downloading...");
                                    ImGui::Text("%.1f%%", progress);
                                    ImGui::Text("%s / %s",
                                        FormatBytes(current).c_str(),
                                        FormatBytes(total).c_str());
                                    ImGui::EndTooltip();
                                }
                            }
                            else
                            {
                                bool isLocked = map.value("locked", false);
                                bool hasPassword = map.value("has_password", false);
                                if (!isLocked)
                                {                                // Disable button Download nếu đang có map khác download
                                    if (isAnyMapDownloading)
                                    {
                                        ImGui::BeginDisabled(true);
                                    }

                                    if (hasPassword)
                                    {
                                        // Màu đỏ/cam cho password
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.40f, 0.20f, 1.00f));
                                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.50f, 0.30f, 1.00f));
                                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.30f, 0.10f, 1.00f));
                                    }
                                    else
                                    {
                                        // Màu xanh bình thường
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.00f));
                                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.00f));
                                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.40f, 0.65f, 1.00f));
                                    }

                                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

                                    // Text rõ ràng hơn
                                    const char* buttonText;
                                    if (hasPassword)
                                    {
                                        buttonText = g_SelecteNgonNgu ? "Enter Pass" : "Nhập Pass";
                                    }
                                    else
                                    {
                                        buttonText = g_SelecteNgonNgu ? "Download" : "Tải về";
                                    }

                                    if (ImGui::Button(buttonText, ImVec2(80, 20)))
                                    {
                                        uint64_t mapSize = map.value("size", 0);
                                        int mapIndex = static_cast<int>(i);

                                        if (hasPassword) 
                                        {
                                            // Hiện dialog nhập password
                                            g_PendingDownloadFilename = filename;
                                            g_PendingDownloadSize = mapSize;
                                            g_PendingDownloadIndex = mapIndex;
                                            g_ShowPasswordDialog = true;
                                        }
                                        else 
                                        {
                                            // Download trực tiếp không cần password
                                            StartMapDownloadWithPassword(filename, mapSize, mapIndex, "");
                                        }

                                        // Gọi wrapper function để download
                                        //StartMapDownload(filename, mapSize, mapIndex);
                                    }

                                    // Tooltip khi button bị disable
                                    if (isAnyMapDownloading && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    {
                                        ImGui::SetTooltip("Đang download map khác...\nVui lòng đợi hoàn tất!");
                                    }
                                    else if (hasPassword && ImGui::IsItemHovered())
                                    {
                                        ImGui::SetTooltip(g_SelecteNgonNgu ? "Password required for download" : "Cần mật khẩu để tải về");
                                    }

                                    ImGui::PopStyleColor(3);
                                    ImGui::PopStyleVar(2);
                                    if (isAnyMapDownloading)
                                    {
                                        ImGui::EndDisabled();
                                    }

                                }
                                else
                                {
                                    ImGui::Dummy(ImVec2(80, 20));
                                }
                            }

                            bool isAdmin = IsAdminUser(GGetUserName());
                            if (!isAdmin)
                            {
                                ImGui::BeginDisabled(true);
                            }
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.30f, 0.30f, 1.00f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.15f, 0.15f, 1.00f));
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
							ImGui::SameLine();
                            if (ImGui::Button(g_SelecteNgonNgu ? "Delete" : "Xoá", ImVec2(80, 20)))
                            {
                                ImGui::OpenPopup("ConfirmDelete");
                            }
                            if (!isAdmin && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            {
                                ImGui::SetTooltip(u8"Có trong List Admin Host API\nmới sử dụng được tính năng này");
                            }

                            if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) 
                            {
                                ImGui::Text(g_SelecteNgonNgu ? "Delete map and config?" : "Xoá Map và Cấu hình?");
                                ImGui::Text("%s", filename.c_str());
                                ImGui::Spacing();

                                if (ImGui::Button(g_SelecteNgonNgu ? "Yes" : "Có", ImVec2(120, 0)))
                                {
                                    std::thread([filename]() 
                                    {
                                        InitMapAPI();

                                        bool mapDeleted = g_MapAPI->DeleteMap(GGetUserName(), filename);

                                        // Delete config
                                        std::string configFilename = filename;
                                        size_t dotPos = configFilename.find_last_of('.');
                                        if (dotPos != std::string::npos) 
                                        {
                                            configFilename = configFilename.substr(0, dotPos) + ".cfg";
                                            g_MapAPI->DeleteConfig(GGetUserName(), configFilename);
                                        }

                                        if (mapDeleted) 
                                        {
                                            g_UploadMessage = g_SelecteNgonNgu ? "Map and config deleted!" : "Map và cấu hình xoá thành thông!";
                                            g_UploadMessageType = 1;
                                            LoadMapListAsync();
                                        }
                                        else 
                                        {
                                            g_UploadMessage = g_SelecteNgonNgu ? "Failed to delete map!" : "Lỗi Xoá Map và cấu hình!";
                                            g_UploadMessageType = 2;
                                        }
                                    }).detach();
                                    ImGui::CloseCurrentPopup();
                                }

                                ImGui::SameLine();
                                if (ImGui::Button(g_SelecteNgonNgu ? "Cancel" : "Hủy", ImVec2(120, 0)))
                                {
                                    CloseUploadPopup();
                                }
                                ImGui::EndPopup();
                            }

                            ImGui::PopStyleColor(3);
                            ImGui::PopStyleVar(2);
                            if (!isAdmin)
                            {
                                ImGui::EndDisabled();
                            }
                            ImGui::PopID();
                        }

                        ImGui::EndTable();
                    }
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Spacing();

        if (g_DownloadInProgress)
        {
            std::string currentFile;
            {
                std::lock_guard<std::mutex> lock(g_DownloadMutex);
                currentFile = g_DownloadingFilename;
            }

            double progress = g_DownloadProgress.load();
            uint64_t current = g_DownloadCurrent.load();
            uint64_t total = g_DownloadTotal.load();

            std::string fullPath = g_War3MapsPath + "\\" + currentFile;

            ImGui::Text(g_SelecteNgonNgu ? "Downloading: %s" : "Đang tải xuống: %s", fullPath.c_str());

            // Progress bar lớn hơn
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

            char progressText[128]{};
            sprintf(progressText, "%.1f%% - %s / %s", progress, FormatBytes(current).c_str(), FormatBytes(total).c_str());

            ImGui::ProgressBar(static_cast<float>(progress / 100.0), ImVec2(-100, 25), progressText);

            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button(g_SelecteNgonNgu ? "Cancel" : "Hủy", ImVec2(0, 25)))
            {
                g_DownloadCancelled = true;
                g_DownloadInProgress = false;
                g_UploadMessage = g_SelecteNgonNgu ? "Download cancelled!" : "Đã huỷ tải xuống!";
                g_UploadMessageType = 3;
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Close button
        float buttonWidth = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.4f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));

        if (ImGui::Button(g_SelecteNgonNgu ? "Cancel" : "Đóng", ImVec2(buttonWidth, 35)))
        {
            g_DownloadCancelled = true;
            g_DownloadInProgress = false;
            CloseUploadPopup();
        }

        ImGui::PopStyleColor(3);

        RenderWar3PathPopup();
        RenderDownloadPasswordDialog();

        ImGui::EndPopup();
    }
}

void ClearMessages()
{
    g_UploadMessage = "";
    g_UploadMessageType = 0;
}

bool CloseUploadPopup()
{
    if (showUploadConfigPopup || showChoseMapPopup)
    {
        showUploadConfigPopup = false;
		showChoseMapPopup = false;
        ImGui::CloseCurrentPopup();
        ClearMessages();
        if (g_MapAPI)
        {
            delete g_MapAPI;
            g_MapAPI = nullptr;
        }
        return true;
    }
    return false;
}

// ========================================================================== =
// 1. KIỂM TRA WAR3.EXE ĐANG CHẠY VÀ LẤY ĐƯỜNG DẪN
// ===========================================================================
std::string GetRunningWar3Path()
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return "";

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe32))
    {
        do
        {
            // Tìm war3.exe hoặc Warcraft III.exe
            std::string processName = pe32.szExeFile;
            std::transform(processName.begin(), processName.end(), processName.begin(), ::tolower);

            if (processName == "war3.exe" || processName == "warcraft iii.exe")
            {
                // Mở process để lấy đường dẫn
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                if (hProcess)
                {
                    char processPath[4096]{};
                    DWORD size = 4096;

                    // Windows Vista+ sử dụng QueryFullProcessImageName
                    if (QueryFullProcessImageNameA(hProcess, 0, processPath, &size))
                    {
                        std::string fullPath = processPath;
                        // Lấy thư mục cha (bỏ war3.exe)
                        size_t lastSlash = fullPath.find_last_of("\\/");
                        if (lastSlash != std::string::npos)
                        {
                            CloseHandle(hProcess);
                            CloseHandle(hSnapshot);
                            return fullPath.substr(0, lastSlash);
                        }
                    }
                    CloseHandle(hProcess);
                }
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return "";
}

// ===========================================================================
// 2. KIỂM TRA THƯ MỤC MAPS CÓ TỒN TẠI KHÔNG
// ===========================================================================
bool CheckMapsFolder(const std::string& war3Path, std::string& mapsPath)
{
    if (war3Path.empty())
        return false;

    mapsPath = war3Path + "\\Maps";

    // Kiểm tra xem thư mục Maps có tồn tại không
    DWORD attrib = GetFileAttributesA(mapsPath.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// ===========================================================================
// 3. TẠO THƯ MỤC MAPS NẾU CHƯA CÓ
// ===========================================================================
bool CreateMapsFolder(const std::string& mapsPath)
{
    if (mapsPath.empty())
        return false;

    return CreateDirectoryA(mapsPath.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// ===========================================================================
// 4. POPUP CHỌN ĐƯỜNG DẪN WAR3 (sử dụng SHBrowseForFolder)
// ===========================================================================
std::string BrowseForWar3Folder()
{
    BROWSEINFOA bi = { 0 };
    bi.lpszTitle = u8"Chọn thư mục cài đặt Warcraft III (thư mục chứa war3.exe)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != NULL)
    {
        char path[4096]{};
        if (SHGetPathFromIDListA(pidl, path))
        {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }

    return "";
}

// ===========================================================================
// 5. VALIDATE ĐƯỜNG DẪN WAR3 (kiểm tra có war3.exe không)
// ===========================================================================
bool ValidateWar3Path(const std::string& path)
{
    if (path.empty())
        return false;

    // Kiểm tra war3.exe hoặc Warcraft III.exe
    std::string war3Exe = path + "\\war3.exe";
    std::string war3Exe2 = path + "\\Warcraft III.exe";
    std::string war3Exe3 = path + "\\_retail_\\x86_64\\Warcraft III.exe"; // Reforged

    DWORD attrib1 = GetFileAttributesA(war3Exe.c_str());
    DWORD attrib2 = GetFileAttributesA(war3Exe2.c_str());
    DWORD attrib3 = GetFileAttributesA(war3Exe3.c_str());

    return (attrib1 != INVALID_FILE_ATTRIBUTES && !(attrib1 & FILE_ATTRIBUTE_DIRECTORY)) ||
        (attrib2 != INVALID_FILE_ATTRIBUTES && !(attrib2 & FILE_ATTRIBUTE_DIRECTORY)) ||
        (attrib3 != INVALID_FILE_ATTRIBUTES && !(attrib3 & FILE_ATTRIBUTE_DIRECTORY));
}

// ===========================================================================
// 6. LƯU ĐƯỜNG DẪN WAR3 VÀO CONFIG
// ===========================================================================
bool SaveWar3PathToConfig(const std::string& war3Path)
{
    // Sử dụng CFG global object từ gproxy
    CFG.ReplaceKeyValue("War3Path", war3Path);

    return true;
}

// ===========================================================================
// 7. ĐỌC ĐƯỜNG DẪN WAR3 TỪ CONFIG
// ===========================================================================
std::string LoadWar3PathFromConfig()
{
    std::string war3Path = CFG.GetString("War3Path", "");

    // Validate path từ config
    if (!war3Path.empty() && ValidateWar3Path(war3Path))
    {
        return war3Path;
    }

    return "";
}

// ===========================================================================
// 8. TỰ ĐỘNG PHÁT HIỆN ĐƯỜNG DẪN WAR3
// ===========================================================================
std::string AutoDetectWar3Path()
{
    // 1. Thử lấy từ config trước
    std::string configPath = LoadWar3PathFromConfig();
    if (!configPath.empty())
        return configPath;

    // 2. Kiểm tra War3 đang chạy
    std::string runningPath = GetRunningWar3Path();
    if (!runningPath.empty() && ValidateWar3Path(runningPath))
    {
        SaveWar3PathToConfig(runningPath);
        return runningPath;
    }

    // 3. Tìm trong Registry (Battle.net Launcher)
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Blizzard Entertainment\\Warcraft III",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char buffer[4096]{};
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "InstallPath", NULL, NULL, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
        {
            std::string regPath = buffer;
            if (ValidateWar3Path(regPath))
            {
                RegCloseKey(hKey);
                SaveWar3PathToConfig(regPath);
                return regPath;
            }
        }
        RegCloseKey(hKey);
    }

    // 4. Tìm trong các thư mục phổ biến
    std::vector<std::string> commonPaths = {
        "C:\\Program Files (x86)\\Warcraft III",
        "C:\\Program Files\\Warcraft III",
        "D:\\Warcraft III",
        "E:\\Warcraft III",
        "C:\\Games\\Warcraft III",
        "D:\\Games\\Warcraft III"
    };

    for (const auto& path : commonPaths)
    {
        if (ValidateWar3Path(path))
        {
            SaveWar3PathToConfig(path);
            return path;
        }
    }

    return "";
}

// ===========================================================================
// 9. SETUP DOWNLOAD PATH - HÀM CHÍNH GỌI TRƯỚC KHI DOWNLOAD
// ===========================================================================
bool SetupDownloadPath(std::string& downloadPath, bool& needShowPopup)
{
    // 1. Tự động detect War3 path
    g_War3Path = AutoDetectWar3Path();

    // 2. Nếu không tìm thấy → cần hiển thị popup
    if (g_War3Path.empty())
    {
        needShowPopup = true;
        return false;
    }

    // 3. Kiểm tra thư mục Maps
    if (!CheckMapsFolder(g_War3Path, g_War3MapsPath))
    {
        // Thử tạo thư mục Maps
        if (!CreateMapsFolder(g_War3MapsPath))
        {
            g_UploadMessage = "Cannot create Maps folder!";
            g_UploadMessageType = 2;
            return false;
        }
    }

    // 4. Set download path
    downloadPath = g_War3MapsPath + "\\";
    return true;
}

// ===========================================================================
// 10. XỬ LÝ POPUP CHỌN ĐƯỜNG DẪN WAR3
// ===========================================================================
void RenderWar3PathPopup()
{
    if (!g_ShowWar3PathPopup)
        return;

    ImGui::OpenPopup("War3 Path Required");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("War3 Path Required", NULL, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(u8"Không tìm thấy đường dẫn Warcraft III!");
        ImGui::TextWrapped(u8"Vui lòng chọn thư mục cài đặt Warcraft III (thư mục chứa war3.exe)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Hiển thị path hiện tại
        ImGui::Text(u8"Đường dẫn hiện tại:");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::InputText("##War3Path", g_War3PathBuffer, 4096, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Buttons
        float buttonWidth = 150.0f;
        float spacing = 10.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

        // Browse button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.4f, 0.7f, 1.0f));

        if (ImGui::Button(u8"Chọn thư mục", ImVec2(buttonWidth, 35)))
        {
            std::string selectedPath = BrowseForWar3Folder();
            if (!selectedPath.empty())
            {
                if (ValidateWar3Path(selectedPath))
                {
                    strcpy(g_War3PathBuffer, selectedPath.c_str());
                    g_War3Path = selectedPath;

                    // Kiểm tra và tạo thư mục Maps
                    if (!CheckMapsFolder(g_War3Path, g_War3MapsPath))
                    {
                        CreateMapsFolder(g_War3MapsPath);
                    }

                    // Lưu vào config
                    SaveWar3PathToConfig(g_War3Path);

                    // Đóng popup
                    g_ShowWar3PathPopup = false;
                    ImGui::CloseCurrentPopup();

                    g_UploadMessage = "War3 path saved successfully!";
                    g_UploadMessageType = 1;
                }
                else
                {
                    g_UploadMessage = u8"Thư mục không hợp lệ! Không tìm thấy war3.exe";
                    g_UploadMessageType = 2;
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, spacing);

        // Cancel button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));

        if (ImGui::Button(u8"Hủy", ImVec2(buttonWidth, 35)))
        {
            g_ShowWar3PathPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Thông tin thêm
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImGui::TextWrapped(u8"Lưu ý: Thư mục Maps sẽ được tự động tạo nếu chưa tồn tại.");
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}

// ===========================================================================
// 11. WRAPPER HÀM DOWNLOAD - GỌI TỪ BUTTON
// ===========================================================================
bool StartMapDownload(const std::string& filename, uint64_t mapSize, int mapIndex)
{
    std::string downloadPath;
    bool needShowPopup = false;

    // Setup download path
    if (!SetupDownloadPath(downloadPath, needShowPopup))
    {
        if (needShowPopup)
        {
            g_ShowWar3PathPopup = true;
            return false;
        }
        return false;
    }

    // Bắt đầu download trong thread riêng
    std::thread([filename, downloadPath, mapSize, mapIndex]()
    {
        InitMapAPI();

        // Reset download state
        g_DownloadInProgress = true;
        g_DownloadCancelled = false;
        g_DownloadProgress = 0.0;
        g_DownloadCurrent = 0;
        g_DownloadTotal = mapSize;
        g_DownloadingMapIndex = mapIndex;
        {
            std::lock_guard<std::mutex> lock(g_DownloadMutex);
            g_DownloadingFilename = filename;
        }

        std::string outputPath = downloadPath + filename;

        // Download map với progress callback
        bool ok = g_MapAPI->DownloadMap(
            filename,
            outputPath,
            "",
            [](double percent, uint64_t current, uint64_t total)
            {
                g_DownloadProgress = percent;
                g_DownloadCurrent = current;
                g_DownloadTotal = total;
            }
        );

        // Check nếu bị cancel
        if (g_DownloadCancelled)
        {
            remove(outputPath.c_str());
            g_UploadMessage = g_SelecteNgonNgu ? "Download cancelled!" : "Đã huỷ tải xuống!";
            g_UploadMessageType = 3;
        }
        else if (ok)
        {
            g_UploadMessage = g_SelecteNgonNgu ? ("Download Map OK! Saved to: " + outputPath) : ("Tải Map thành công! Đã lưu vào: " + outputPath);
            g_UploadMessageType = 1;
        }
        else
        {
            g_UploadMessage = g_SelecteNgonNgu ? "Failed to download map!" : "Lỗi tải Map!";
            g_UploadMessageType = 2;
        }

        // Reset download state
        g_DownloadInProgress = false;
        g_DownloadingMapIndex = -1;
        g_DownloadCancelled = false;
    }).detach();

    return true;
}

void StartMapDownloadWithPassword(const std::string& filename, uint64_t mapSize, int mapIndex, const std::string& password)
{
    std::string downloadPath;
    bool needShowPopup = false;

    if (!SetupDownloadPath(downloadPath, needShowPopup))
    {
        if (needShowPopup)
            g_ShowWar3PathPopup = true;
        return;
    }

    // Download trong thread
    std::thread([filename, downloadPath, mapSize, mapIndex, password]()
    {
        InitMapAPI();

        g_DownloadInProgress = true;
        g_DownloadCancelled = false;
        g_DownloadProgress = 0.0;
        g_DownloadCurrent = 0;
        g_DownloadTotal = mapSize;
        g_DownloadingMapIndex = mapIndex;
        {
            std::lock_guard<std::mutex> lock(g_DownloadMutex);
            g_DownloadingFilename = filename;
        }

        std::string outputPath = downloadPath + filename;

        // Download với password
        bool ok = g_MapAPI->DownloadMap(
            filename,
            outputPath,
            password,  // TRUYỀN PASSWORD VÀO
            [](double percent, uint64_t current, uint64_t total)
            {
                g_DownloadProgress = percent;
                g_DownloadCurrent = current;
                g_DownloadTotal = total;
            }
        );

        if (g_DownloadCancelled)
        {
            remove(outputPath.c_str());
            g_UploadMessage = g_SelecteNgonNgu ? "Download cancelled!" : "Đã huỷ tải xuống!";
            g_UploadMessageType = 3;
        }
        else if (ok)
        {
            g_UploadMessage = g_SelecteNgonNgu ? "Download OK!" : "Tải thành công!";
            g_UploadMessageType = 1;
        }
        else
        {
            // Có thể là sai password hoặc lỗi khác
            g_UploadMessage = g_SelecteNgonNgu ? "Download failed! Check password." : "Tải thất bại! Kiểm tra mật khẩu.";
            g_UploadMessageType = 2;
        }

        g_DownloadInProgress = false;
        g_DownloadingMapIndex = -1;
        g_DownloadCancelled = false;
    }).detach();
}

void InitializeWar3Path()
{
    // Load từ config
    g_War3Path = LoadWar3PathFromConfig();

    // Validate và setup
    if (!g_War3Path.empty() && ValidateWar3Path(g_War3Path))
    {
        CheckMapsFolder(g_War3Path, g_War3MapsPath);
        if (g_War3MapsPath.empty())
        {
            g_War3MapsPath = g_War3Path + "\\Maps";
            CreateMapsFolder(g_War3MapsPath);
        }
    }
    else
    {
        // Path không hợp lệ → reset
        g_War3Path = "";
        g_War3MapsPath = "";
    }
}