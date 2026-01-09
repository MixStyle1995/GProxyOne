/*

   Copyright 2010 Trevor Hogan

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

// todotodo: GHost++ may drop the player even after they reconnect if they run out of time and haven't caught up yet

#include "gproxy.h"
#include "util.h"
#include "socket.h"
#include "commandpacket.h"
#include "bnetprotocol.h"
#include "bnet.h"
#include "gameprotocol.h"
#include "gpsprotocol.h"

// Replaced PDcurses with ImGui
// #include "curses.h"
#include "gproxy_imgui.h"
#include "CMDNSScanner.h"

#include <signal.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock.h>
#include <process.h>
#include <time.h>
#include <unordered_set>
#include <unordered_map>
#include <fcntl.h>
#include <io.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <Urlmon.h>
#include <wininet.h>
#include <thread>
#include <mutex>

#include "Obuscate.hpp"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "urlmon.lib")

//#include <d3d9.h>
//#pragma comment(lib, "d3d9.lib")

//#include <imgui.h>
//#include <backends/imgui_impl_win32.h>
//#include <backends/imgui_impl_dx9.h>

#include <asio.hpp>

using asio::ip::tcp;

//void GuiThread();

bool gCurses = false;
vector<ColoredLine> gMainBuffer;
string gInputBuffer;
string gChannelName;
vector<string> gChannelUsers;
// Removed for ImGui: WINDOW *gMainWindow = NULL;
// Removed for ImGui: WINDOW *gBottomBorder = NULL;
// Removed for ImGui: WINDOW *gRightBorder = NULL;
// Removed for ImGui: WINDOW *gInputWindow = NULL;
// Removed for ImGui: WINDOW *gChannelWindow = NULL;
bool gMainWindowChanged = false;
bool gInputWindowChanged = false;
bool gChannelWindowChanged = false;

string gLogFile;
CGProxy *gGProxy = NULL;

uint32_t War3Version = 24;
bool          gRestart = false;

string szIpUpFileAuraBot;
CConfig CFG;

// ImGui thread handle
std::thread* g_ImGuiThread = nullptr;

uint32_t GetTime( )
{
	return GetTicks( ) / 1000;
}

uint32_t GetTicks( )
{
	return timeGetTime( );
}

void LOG_Print( string message )
{
	if( !gLogFile.empty( ) )
	{
		ofstream Log;
		Log.open( gLogFile.c_str( ), ios :: app );

		if( !Log.fail( ) )
		{
			time_t Now = time( NULL );
			string Time = asctime( localtime( &Now ) );

			// erase the newline

			Time.erase( Time.size( ) - 1 );
			Log << "[" << Time << "] " << message << endl;
			Log.close( );
		}
	}
}

void CONSOLE_Print( string message, int color, bool log, int tsline)
{
	CONSOLE_PrintNoCRLF( message, color, log, tsline);

	if (gWaitingForGames) {
		gGamesRawOutput += message;

		uint32_t currentTime = GetTime();
		if (currentTime - gGamesStartTime > 5) {
			ParseGamesOutput(gGamesRawOutput);
			gGamesRawOutput.clear();
		}
		else if (message.find("Total:") != std::string::npos ||
			message.find("total:") != std::string::npos ||
			message.find("games displayed") != std::string::npos ||
			message.find("No games") != std::string::npos) {
			ParseGamesOutput(gGamesRawOutput);
			gGamesRawOutput.clear();
		}
	}

	if (!gCurses)
		cout << endl;

	scrollToBottom = true;
}

const unordered_map<wstring, int> tagColors = 
{
	{ L"[BNET]", dye_light_red },
	{ L"[INFO]", dye_light_aqua },
	{ L"[ERROR]", dye_light_yellow },
	{ L"[GPROXY]", dye_light_green },
	{ L"[TCPCLIENT]", dye_light_green },
};

void CONSOLE_PrintNoCRLF(string message, int color, bool log, int tsline)
{
	if (gGProxy && !gGProxy->m_Console)
		return;

	// Safety check: Nếu message rỗng, return sớm
	if (message.empty())
		return;

	string::size_type loc = message.find("]", 0);

	if (gGProxy && !gGProxy->m_inconsole && gGProxy->m_UDPConsole)
	{
		gGProxy->UDPChatSend(message.c_str());
		if (message.find("]") == string::npos)
			gGProxy->UDPChatSend("|Chate " + UTIL_ToString(gGProxy->m_Username.length()) + " " + gGProxy->m_Username + " " + message);
		size_t pos = message.find("GPROXY]");
		if (pos != string::npos)
			gGProxy->UDPChatSend("|Chate " + UTIL_ToString(gGProxy->m_Username.length()) + " " + gGProxy->m_Username + " " + message.substr(pos + 8, message.length()));
	}

	ColoredLine line = {};
	wstring wmessage;
	
	// Safety check: Bảo vệ việc convert UTF-8 sang Wide string
	try
	{
		wmessage = Utf8ToWide(message);
	}
	catch (...)
	{
		// Nếu convert thất bại, tạo wstring trực tiếp từ ASCII
		wmessage = wstring(message.begin(), message.end());
	}

	// Safety check: Nếu wmessage rỗng sau khi convert
	if (wmessage.empty())
		return;

	bool matched = false;
	for (const auto& concac : tagColors)
	{
		if (wmessage.find(concac.first) == 0)
		{
			line.segments.push_back({ concac.first, concac.second });
			
			// Safety check: Đảm bảo substring không vượt quá độ dài
			if (wmessage.length() > concac.first.length())
			{
				line.segments.push_back({ wmessage.substr(concac.first.length()), color });
			}
			
			matched = true;
			break;
		}
	}

	if (!matched)
		line.segments.push_back({ wmessage, color });

	if (!line.segments.empty())
	{
		gMainBuffer.push_back(line);
		if (gMainBuffer.size() > 100)
			gMainBuffer.erase(gMainBuffer.begin());
	}
	
	gMainWindowChanged = true;
	// CONSOLE_Draw is now handled in ImGui thread

	if( log )
		LOG_Print( message );

	if (!gCurses)
	{
		cout << message;
	}
}

void CONSOLE_ChangeChannel( string channel )
{
	gChannelName = channel;
	gChannelWindowChanged = true;
}

void CONSOLE_AddChannelUser( string name )
{
	for( vector<string> :: iterator i = gChannelUsers.begin( ); i != gChannelUsers.end( ); i++ )
	{
		if( *i == name )
			return;
	}

	gChannelUsers.push_back( name );
	gGProxy->UDPChatSend("|channeljoin "+name);
	gChannelWindowChanged = true;
}

void CONSOLE_RemoveChannelUser( string name )
{
	for( vector<string> :: iterator i = gChannelUsers.begin( ); i != gChannelUsers.end( ); )
	{
		if( *i == name )
			i = gChannelUsers.erase( i );
		else
			i++;
	}
	gGProxy->UDPChatSend("|channelleave "+name);
	gChannelWindowChanged = true;
}

void CONSOLE_RemoveChannelUsers( )
{
	gChannelUsers.clear( );
	gChannelWindowChanged = true;
}

void CONSOLE_Draw(int tsline)
{
	// No-op for ImGui - drawing happens in GuiThread
}

void CONSOLE_Resize()
{
	// No-op for ImGui - automatic window resize
}

bool sendAll(tcp::socket& socket, const void* buf, size_t len)
{
	const char* p = (const char*)buf;
	size_t sent = 0;
	/*while (sent < len)
	{
		sent += asio::write(socket, asio::buffer(p + sent, len - sent));
	}*/
	return true;
}

void uploadmap(string szIp, string MapDir, uint32_t version)
{
	removeSubstrs<char>(MapDir, "\"");

	if (MapDir.empty())
	{
		CONSOLE_Print(u8"[ERROR] Không thấy đường dẫn File");
		return;
	}

	std::ifstream in(MapDir, std::ios::binary | std::ios::ate);
	if (!in)
	{
		CONSOLE_Print(u8"[ERROR] Không mở được file");
		return;
	}

	size_t pos = MapDir.find_last_of("/\\");
	std::string name = (pos != std::string::npos) ? MapDir.substr(pos + 1) : MapDir;

	in.seekg(0, std::ios::end);
	uint64_t fileSize = in.tellg();
	in.seekg(0);

	asio::io_context io;
	tcp::socket socket(io);
	socket.connect({ asio::ip::make_address(szIp), 9000 });

	asio::ip::tcp::no_delay option(true);
	socket.set_option(option);

	int sndbuf = 1 << 20; // 1MB buffer
	setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));

	uint16_t nameLen = (uint16_t)name.size();
	sendAll(socket, &nameLen, sizeof(nameLen));
	sendAll(socket, &fileSize, sizeof(fileSize));
	sendAll(socket, &version, sizeof(version));
	sendAll(socket, name.data(), name.size());

	std::vector<char> buf(1 << 19);
	uint64_t sent = 0;
	auto start = std::chrono::steady_clock::now();

	while (in)
	{
		in.read(buf.data(), buf.size());
		std::streamsize n = in.gcount();
		if (n <= 0) break;
		sendAll(socket, buf.data(), (size_t)n);
		sent += (uint64_t)n;

		// hiển thị tiến độ
		double uploadedMB = sent / (1024.0 * 1024.0);
		double totalMB = fileSize / (1024.0 * 1024.0);
		int percent = (totalMB > 0) ? (int)((uploadedMB / totalMB) * 100) : 0;

		// tính tốc độ MB/s
		auto now = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now - start).count();
		double speedMBps = (elapsed > 0) ? (uploadedMB / elapsed) : 0.0;

		std::ostringstream oss;
		oss << "Tiến độ: " << (int)uploadedMB << " / " << (int)totalMB << " MB " << "(" << percent << "%) - " << std::fixed << std::setprecision(2) << speedMBps << " MB/s";;

		/*if (speedMBps < 1000.0)
			oss << (int)speedMBps << " KB/s      ";
		else
			oss << (int)speedMBps << " MB/s      ";*/

		CONSOLE_Print(u8"[INFO] " + oss.str(), 0, 0, 1);
	}
	in.close();
	socket.close();
	CONSOLE_Print(u8"[INFO] Upload File thành công");
}

DWORD GetProcessIdByName(const string& name)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return 0;

	PROCESSENTRY32 pe;
	pe.dwSize = sizeof(pe);

	if (Process32First(snapshot, &pe))
	{
		do {
			if (_stricmp(pe.szExeFile, name.c_str()) == 0)
			{
				CloseHandle(snapshot);
				return pe.th32ProcessID;
			}
		} while (Process32Next(snapshot, &pe));
	}

	CloseHandle(snapshot);
	return 0;
}

bool InjectDLL(DWORD pid, const char* dllPath)
{
	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (!hProc) return false;

	size_t len = strlen(dllPath) + 1;

	LPVOID alloc = VirtualAllocEx(hProc, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!alloc)
	{
		CloseHandle(hProc);
		return false;
	}

	WriteProcessMemory(hProc, alloc, dllPath, len, NULL);

	HANDLE hThread = CreateRemoteThread(
		hProc, NULL, 0,
		(LPTHREAD_START_ROUTINE)LoadLibraryA,
		alloc, 0, NULL
	);

	if (!hThread)
	{
		VirtualFreeEx(hProc, alloc, 0, MEM_RELEASE);
		CloseHandle(hProc);
		return false;
	}

	WaitForSingleObject(hThread, INFINITE);

	CloseHandle(hThread);
	VirtualFreeEx(hProc, alloc, 0, MEM_RELEASE);
	CloseHandle(hProc);
	return true;
}

string GetFullDLLPath(const string& dllName)
{
	char path[4096] = {};
	GetModuleFileNameA(NULL, path, 4096);
	string exePath = path;

	size_t pos = exePath.find_last_of("\\/");
	exePath = exePath.substr(0, pos + 1);

	return exePath + dllName;
}

bool is_file_exist(string fileName)
{
	std::ifstream infile(fileName);
	return infile.good();
}

std::string WCharToUTF8(const wchar_t* w)
{
	if (!w) return {};

	int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL );
	std::string str(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, w, -1, &str[0], sizeNeeded, NULL, NULL);
	if (!str.empty() && str.back() == '\0')
		str.pop_back();

	return str;
}

std::wstring GetProcessPathW(DWORD pid)
{
	wchar_t path[4096] = { 0 };
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!hProcess)
		return L"";

	if (GetModuleFileNameExW(hProcess, NULL, path, 4096))
	{
		CloseHandle(hProcess);
		return std::wstring(path);
	}

	CloseHandle(hProcess);
	return L"";
}

HMODULE FindModuleByName(DWORD pid, std::wstring fileName)
{
	HMODULE hMods[4096] = {};
	DWORD cbNeeded;

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!hProcess)
		return NULL;

	if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
	{
		int count = cbNeeded / sizeof(HMODULE);
		wchar_t modPath[4096] = {};

		for (int i = 0; i < count; i++)
		{
			if (GetModuleFileNameExW(hProcess, hMods[i], modPath, 4096))
			{
				if (wstring(modPath) == fileName)
				{
					CloseHandle(hProcess);
					return hMods[i];
				}
			}
		}
	}

	CloseHandle(hProcess);
	return NULL;
}

bool UnloadModule(DWORD pid, HMODULE hModule)
{
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (!hProcess) return false;

	LPTHREAD_START_ROUTINE pFreeLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "FreeLibrary");

	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pFreeLibrary, hModule, 0, NULL);

	if (!hThread)
	{
		CloseHandle(hProcess);
		return false;
	}

	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
	CloseHandle(hProcess);
	return true;
}

HANDLE WriteAccountToSharedMemory(const std::string& text)
{
	HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 1024, "Global\\gproxy_thaison_SharedData");
	if (!hMap) return nullptr;

	void* ptr = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (!ptr)
	{
		CloseHandle(hMap);
		return nullptr;
	}

	strcpy_s((char*)ptr, 1024, text.c_str());

	UnmapViewOfFile(ptr);
	return ptr;
}

DWORD WINAPI InjectThread(LPVOID)
{
	string fileets = GetFullDLLPath("ETS.mix");
	DeleteFile(fileets.c_str());
	string strUrl = "https://github.com/MixStyle1995/project_ts/raw/master/ETS.mix";
	DeleteUrlCacheEntry(strUrl.c_str());
	HRESULT res = URLDownloadToFile(NULL, strUrl.c_str(), "ETS.mix", 0, NULL);
	if (res != S_OK)
	{
		DeleteUrlCacheEntry(strUrl.c_str());
		return 0;
	}
	else
	{
		while (true)
		{
			HANDLE hMap = nullptr;
			HANDLE hMonitor = CreateMutex(NULL, FALSE, "Global\\gproxy_thaison");
			if (!hMonitor)
			{
				CONSOLE_Print(u8"[GPROXY] Không tạo được mutex!");
				Sleep(1000);
				return 0;
			}

			DWORD pid = GetProcessIdByName("war3.exe");
			if (pid == 0)
			{
				Sleep(1000);
				continue;
			}

			std::wstring war3Exe = GetProcessPathW(pid);
			std::wstring war3Dir = war3Exe.substr(0, war3Exe.find_last_of(L"\\/"));
			std::wstring targetMix = war3Dir + L"\\ETS.mix";

			//CONSOLE_Print(u8"[GPROXY] " + string(war3Exe.begin(), war3Exe.end()));
			//CONSOLE_Print(u8"[GPROXY] " + string(targetMix.begin(), targetMix.end()));

			HMODULE hWar3Mix = FindModuleByName(pid, targetMix);
			if (hWar3Mix)
			{
				if (UnloadModule(pid, hWar3Mix))
				{
					CONSOLE_Print(u8"[GPROXY] Free ETS.mix thành công!");
					DeleteFileW(targetMix.c_str());
				}
				else
				{
					CONSOLE_Print(u8"[GPROXY] Free ETS.mix thất bại!");
				}
			}

			if (!is_file_exist(fileets))
			{
				res = URLDownloadToFile(NULL, strUrl.c_str(), "ETS.mix", 0, NULL);
				if (res != S_OK)
				{
					DeleteUrlCacheEntry(strUrl.c_str());
					Sleep(1000);
					continue;
				}
			}
			else
			{
				CONSOLE_Print(u8"[GPROXY] Đã tìm thấy war3.exe, PID = " + to_string(pid));

				if (InjectDLL(pid, fileets.c_str()))
				{
					CONSOLE_Print(u8"[GPROXY] Mutex thành công");
					if (gGProxy) 
						hMap = WriteAccountToSharedMemory(gGProxy->m_Username);
				}
				else
					CONSOLE_Print(u8"[GPROXY] Mutex thất bại!");

				while (true)
				{
					Sleep(1000);

					DWORD stillRunning = GetProcessIdByName("war3.exe");

					if (stillRunning != pid)
					{
						CONSOLE_Print(u8"[GPROXY] war3.exe đã tắt → giải phóng Mutex");
						DeleteFile(fileets.c_str());
						DeleteFileW(targetMix.c_str());
						if (hMap) CloseHandle(hMap);
						if (hMonitor)
						{
							ReleaseMutex(hMonitor);
							CloseHandle(hMonitor);
						}
						break;
					}
				}
			}
		}
	}

	return 0;
}

void RegisterNewAccount(const char* username, const char* password, const char* email)
{
	if (gGProxy)
	{
		if (gGProxy->m_BNET && gGProxy->m_BNET->m_Socket)
		{
			if (!gGProxy->m_BNET->m_Socket->HasError() && gGProxy->m_BNET->m_Socket->GetConnected())
			{
				//	MessageBoxA( 0, "SendRegisterPacket 4", "ALL", 0 );

				BYTEARRAY buildpacket = BYTEARRAY();
				uint32_t packetid = 0;
				UTIL_AppendByteArray(buildpacket, packetid, 4);
				UTIL_AppendByteArrayFast(buildpacket, string(username));		// Account Name
				UTIL_AppendByteArrayFast(buildpacket, string(password));		// Account Name
				UTIL_AppendByteArrayFast(buildpacket, string(email));		// Account Name
				gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_WC3_CLIENT(buildpacket));
			}
			else if (gGProxy->m_BNET->m_Socket->HasError())
			{
				//MessageBoxA(0, "SendRegisterPacket ERROR 1", "ALL", 0);
			}
			else if (!gGProxy->m_BNET->m_Socket->GetConnected())
			{
				//MessageBoxA(0, "SendRegisterPacket ERROR 2", "ALL", 0);
			}
		}
		else
		{
			//MessageBoxA(0, "SendRegisterPacket ERROR 4", "ALL", 0);
		}
	}
	else
	{
		//MessageBoxA(0, "SendRegisterPacket ERROR 3", "ALL", 0);
	}
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

inline std::vector<uint8_t> CreateByteArray(const uint32_t i, bool reverse)
{
	if (!reverse)
		return std::vector<uint8_t>{static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 24)};
	else
		return std::vector<uint8_t>{static_cast<uint8_t>(i >> 24), static_cast<uint8_t>(i >> 16), static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i)};
}

void Process_Command( )
{
	string Command = gInputBuffer;
	transform( Command.begin( ), Command.end( ), Command.begin( ), (int(*)(int))tolower );

	if( Command == "#commands" || Command == "#command")
	{
		CONSOLE_Print( ">>> #commands" );
		CONSOLE_Print( "", 0, false );
		CONSOLE_Print( "  In the GProxy++ console:", dye_light_purple, false );
		CONSOLE_Print( "   #commands           : show command list", 0, false );
		CONSOLE_Print( "   #exit or #quit      : close GProxy++", 0, false );
		CONSOLE_Print( "   #filter <f>         : start filtering public game names for <f>", 0, false );
		CONSOLE_Print( "   #filteroff          : stop filtering public game names", 0, false );
		CONSOLE_Print( "   #game <gamename>    : look for a specific game named <gamename>", 0, false );
		CONSOLE_Print( "   #help               : show help text", 0, false );
		CONSOLE_Print( "   #public             : enable listing of public games", 0, false );
		CONSOLE_Print( "   #publicoff          : disable listing of public games", 0, false );
		CONSOLE_Print( "   #version            : show version text", 0, false );
		CONSOLE_Print( u8"   #war                : Thay đổi phiên bản war3", 0, false);
		CONSOLE_Print( "", 0, false);
		CONSOLE_Print( "  In Game: ", dye_light_purple, false);
		CONSOLE_Print( "   /r <message>        : reply to the last received whisper", 0, false);
		CONSOLE_Print( "   /re <message>       : reply to the last received whisper", 0, false );
		CONSOLE_Print( "   /sc                 : whispers \"spoofcheck\" to the game host", 0, false );
		CONSOLE_Print( "   /status             : show status information", 0, false );
		CONSOLE_Print( "   /w <user> <message> : whispers <message> to <user>", 0, false );
		CONSOLE_Print( "", 0, false );
	}
	else if( Command == "#exit" || Command == "#quit" )
	{
		gGProxy->m_Quit = true;
		return;
	}
	else if (Command == "#cf")
	{
		const char* maps[] = {
				"Maps\\FrozenThrone\\(12)IceCrown.w3x",
				"Maps\\FrozenThrone\\(8)Terenas_Stand.w3x",
				"Maps\\FrozenThrone\\(4)TwistedMeadows.w3x",
				"Maps\\Download\\DotA v6.88.w3x",
				"Maps\\(12)EmeraldGardens.w3m"
		};

		const char* games[] = 
		{
			"Game1",
			"Game11",
			"Game111",
			"Game1111",
			"Game11111"
		};

		const char* hosts[] = {
			"Player1",
			"ProGamer",
			"TestHost",
			"WarCraft3",
			"MapMaker"
		};

		const char* hostsowner[] = {
			"Aliaavsdv",
			"Beatrizsdv",
			"Charlessdv",
			"Diyavsds",
			"Ericsdvsdv"
		};

		for (int i = 0; i < 5; i++)
		{
			vector<uint8_t> MapWidth;
			MapWidth.push_back(192);
			MapWidth.push_back(7);
			vector<uint8_t> MapHeight;
			MapHeight.push_back(192);
			MapHeight.push_back(7);

			uint32_t GameType = MAPGAMETYPE_MAKERUSER | MAPGAMETYPE_TYPESCENARIO | MAPGAMETYPE_SIZELARGE | MAPGAMETYPE_OBSNONE;
			uint32_t GameFlags = 0x00000002 | 0x00000800 | 0x00002000 | 0x04000000;

			uint32_t uniqueHostCounter = ((uint32_t)time(NULL) + i) | (i << 24);

			std::vector<uint8_t>	m_MapCRC = UTIL_ExtractNumbers("108 250 204 59", 4);
			std::vector<uint8_t>	m_MapSHA1 = UTIL_ExtractNumbers("35 81 104 182 223 63 204 215 1 17 87 234 220 66 3 185 82 99 6 13", 20);
			gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_STARTADVEX3(
				GAME_PUBLIC, CreateByteArray(GameType, false), CreateByteArray(GameFlags, false), MapWidth, MapHeight, games[i], hosts[i], hostsowner[i], 0, maps[i], m_MapCRC, m_MapSHA1, uniqueHostCounter, 12, 10));

			CONSOLE_Print(u8"[BNET] " + string(hosts[i]) + string(games[i]), dye_light_purple);
		}

		//gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_REQUEST_GAME_LIST());
		//CONSOLE_Print(u8"[BNET] Send REQUEST_GAME_LIST", dye_light_purple);
	}
	else if (Command == "#test")
	{
		gGProxy->m_BNET->m_Socket->PutBytes(gGProxy->m_BNET->m_Protocol->SEND_SID_REQUEST_GAME_LIST());
		CONSOLE_Print(u8"[BNET] Send REQUEST_GAME_LIST", dye_light_purple);
	}
	else if (Command == "#rf")
	{
		gGProxy->m_BNET->QueueGetGameList(20);
		CONSOLE_Print(u8"[BNET] Làm mới danh sách trò chơi", dye_light_purple);
	}
	else if (Command.size() >= 6 && Command.substr(0, 5) == "#reg ")
	{
		string username = gInputBuffer.substr(5);
		string password = gInputBuffer.substr(5);
		string email = gInputBuffer.substr(5);
		RegisterNewAccount(username.c_str(), password.c_str(), email.c_str());
		CONSOLE_Print(u8"[INFO] Đăng ký tài khoản: " + username);
	}
	else if (Command.size() >= 6 && Command.substr(0, 5) == "#sip ")
	{
		szIpUpFileAuraBot = gInputBuffer.substr(5);
		CONSOLE_Print(u8"[INFO] Set Server Upload File: " + szIpUpFileAuraBot);
	}
	else if (Command.size() >= 7 && (Command.substr(0, 6) == "#up24 " || Command.substr(0, 6) == "#up26 " || Command.substr(0, 6) == "#up27 " || Command.substr(0, 6) == "#up28 " || Command.substr(0, 6) == "#up29 " || Command.substr(0, 6) == "#up31 "))
	{
		if (!szIpUpFileAuraBot.empty())
		{
			string war3ver = Command.substr(3, 3);
			string mapDir = gInputBuffer.substr(6);
			uploadmap(szIpUpFileAuraBot, mapDir, atoi(war3ver.c_str()));
		}
		else
		{
			CONSOLE_Print(u8"[ERROR] Chưa nhập IP Server Upload, hãy dùng lênh #sip [địa chỉ IP]");
		}
	}
	else if (Command.size() >= 6 && Command.substr(0, 5) == "#war ")
	{
		string war3ver = gInputBuffer.substr(5);
		if (war3ver != "24" && war3ver != "26" && war3ver != "27" && war3ver != "28" && war3ver != "29" && war3ver != "31")
		{
			CONSOLE_Print(u8"[BNET] Nhập không đúng phiên bản");
		}
		else
		{
			uint32_t ver = atoi(war3ver.c_str());
			if (ver == 24) selectedWar3Version = 0;
			else if (ver == 26) selectedWar3Version = 1;
			else if (ver == 27) selectedWar3Version = 2;
			else if (ver == 28) selectedWar3Version = 3;
			else if (ver == 29) selectedWar3Version = 4;
			else if (ver == 31) selectedWar3Version = 5;

			gGProxy->m_War3Version = atoi(war3ver.c_str());
			gGProxy->m_BNET->m_War3Version = atoi(war3ver.c_str());
			CONSOLE_Print(u8"[INFO] Đã chọn War3 version 1." + war3ver, dye_light_green);
		}
	}

	else if( Command.size( ) >= 9 && Command.substr( 0, 8 ) == "#filter " )
	{
		string Filter = gInputBuffer.substr( 8 );

		if( !Filter.empty( ) && Filter.size( ) <= 31 )
		{
			gGProxy->m_BNET->SetPublicGameFilter( Filter );
			CONSOLE_Print( "[BNET] started filtering public game names for \"" + Filter + "\"" );
		}
	}
	else if( Command == "#filteroff" )
	{
		gGProxy->m_BNET->SetPublicGameFilter( string( ) );
		CONSOLE_Print( "[BNET] stopped filtering public game names" );
	}
	else if( Command.size( ) >= 7 && Command.substr( 0, 6 ) == "#game " )
	{
		string GameName = gInputBuffer.substr( 6 );

		if( !GameName.empty( ) && GameName.size( ) <= 31 )
		{
			gGProxy->m_BNET->SetSearchGameName( GameName );
			CONSOLE_Print( "[BNET] looking for a game named \"" + GameName + "\" for up to two minutes" );
		}
	}
	else if( Command == "#help" )
	{
		CONSOLE_Print( ">>> /help" );
		CONSOLE_Print( "", false );
		CONSOLE_Print( "  GProxy++ connects to battle.net and looks for games for you to join.", false );
		CONSOLE_Print( "  If GProxy++ finds any they will be listed on the Warcraft III LAN screen.", false );
		CONSOLE_Print( "  To join a game, simply open Warcraft III and wait at the LAN screen.", false );
		CONSOLE_Print( "  Standard games will be white and GProxy++ enabled games will be blue.", false );
		CONSOLE_Print( "  Note: You must type \"/public\" to enable listing of public games.", false );
		CONSOLE_Print( "", false );
		CONSOLE_Print( "  If you want to join a specific game, type \"/game <gamename>\".", false );
		CONSOLE_Print( "  GProxy++ will look for that game for up to two minutes before giving up.", false );
		CONSOLE_Print( "  This is useful for private games.", false );
		CONSOLE_Print( "", false );
		CONSOLE_Print( "  Please note:", false );
		CONSOLE_Print( "  GProxy++ will join the game using your battle.net name, not your LAN name.", false );
		CONSOLE_Print( "  Other players will see your battle.net name even if you choose another name.", false );
		CONSOLE_Print( "  This is done so that you can be automatically spoof checked.", false );
		CONSOLE_Print( "", false );
		CONSOLE_Print( "  Type \"/commands\" for a full command list.", false );
		CONSOLE_Print( "", false );
	}
	else if( Command == "#public" || Command == "#publicon" || Command == "#public on" || Command == "#list" || Command == "#liston" || Command == "#list on" )
	{
		gGProxy->m_BNET->SetListPublicGames( true );
		gGProxy->m_BNET->SetSearchGameName("");
		CONSOLE_Print( "[BNET] listing of public games enabled" );
	}
	else if( Command == "#publicoff" || Command == "#public off" || Command == "#listoff" || Command == "#list off" )
	{
		gGProxy->m_BNET->SetListPublicGames( false );
		CONSOLE_Print( "[BNET] listing of public games disabled" );
	}
	else if( Command.size( ) >= 4 && Command.substr( 0, 3 ) == "#r " )
	{
		if( !gGProxy->m_BNET->GetReplyTarget( ).empty( ) )
			gGProxy->m_BNET->QueueChatCommand( gInputBuffer.substr( 3 ), gGProxy->m_BNET->GetReplyTarget( ), true );
		else
			CONSOLE_Print( "[BNET] nobody has whispered you yet" );
	}
	else if( Command == "#version" )
		CONSOLE_Print( "[GPROXY] GProxy++ Version " + gGProxy->m_Version );
	else
		gGProxy->m_BNET->QueueChatCommand( gInputBuffer );

	gInputBuffer.clear( );
}
//
// main
//

int main(int argc, char **argv)
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	wchar_t dirW[4096] = {};
	GetCurrentDirectoryW(4096, dirW);

	std::wstring cfgPathW = std::wstring(dirW) + L"\\gproxy.cfg";
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, cfgPathW.c_str(), -1, NULL, 0, NULL, NULL);
	std::string cfgPath(size_needed - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, cfgPathW.c_str(), -1, &cfgPath[0], size_needed, NULL, NULL);

	CFG.Read(cfgPath);
	gLogFile = CFG.GetString( "log", string( ) );

	CONSOLE_Print( "[GPROXY] starting winsock" );

	WSADATA wsadata;
	if( WSAStartup( MAKEWORD( 2, 2 ), &wsadata ) != 0 )
	{
		CONSOLE_Print( "[GPROXY] error starting winsock" );
		return 1;
	}

	CONSOLE_Print( "[GPROXY] setting process priority to \"above normal\"" );
	SetPriorityClass( GetCurrentProcess( ), ABOVE_NORMAL_PRIORITY_CLASS );

	string War3Path;
	string CDKeyROC;
	string CDKeyTFT;
	string Server;
	string Username;
	string Password;
	string Channel;
	uint16_t Port = 6125;
	BYTEARRAY EXEVersion;
	BYTEARRAY EXEVersionHash;
	string PasswordHashType;
	string UDPBindIP;
	uint16_t UDPPort=6959;
	uint16_t GUIPort=5858;
	string Hosts;
	string UDPPassword;
	string UDPTrigger = ".";
	bool PublicGames = true;
	bool UDPConsole = true;
	bool FilterGProxy = false;

	War3Path = "";
	CDKeyROC = CFG.GetString( "cdkeyroc", "FFFFFFFFFFFFFFFFFFFFFFFFFF");
	CDKeyTFT = CFG.GetString( "cdkeytft", "FFFFFFFFFFFFFFFFFFFFFFFFFF");
	Server = CFG.GetString( "server", string( ) );
	Username = CFG.GetString( "username", string( ) );
	Password = CFG.GetString( "password", string( ) );
	Channel = CFG.GetString( "channel", string( ) );
	War3Version = CFG.GetInt( "war3version", War3Version );
	Port = CFG.GetInt( "port", Port );
	EXEVersion = (BYTEARRAY)0;
	EXEVersionHash = (BYTEARRAY)0;
	PasswordHashType = CFG.GetString( "passwordhashtype", string( ) );
	UDPBindIP = CFG.GetString( "udp_cmdbindip", "0.0.0.0" );
	UDPPort = CFG.GetInt( "udp_cmdport", 6959  );
	GUIPort = CFG.GetInt( "udp_guiport", 5858  );
	UDPConsole = CFG.GetInt( "udp_console", 1 ) == 0 ? false : true;
	PublicGames = CFG.GetInt( "publicgames", 1 ) == 0 ? false : true;
	FilterGProxy = CFG.GetInt( "filtergproxy", 0 ) == 0 ? false : true;
	Hosts = CFG.GetString( "filterhosts", string());
	UDPPassword = CFG.GetString( "udp_password", string () );

	transform( Hosts.begin( ), Hosts.end( ), Hosts.begin( ), (int(*)(int))tolower );

	if (!CFG.Exists("server"))
	{
		CONSOLE_PrintNoCRLF(u8"  Nhập tên máy chủ hoặc IP: ", dye_light_yellow, false);
		getline(cin, Server);
		transform(Server.begin(), Server.end(), Server.begin(), (int(*)(int))tolower);
		if (Server.empty())
			Server = "160.187.146.137";

		CFG.ReplaceKeyValue("server", Server);
	}

	if (!CFG.Exists("username"))
	{
		CONSOLE_PrintNoCRLF(u8"  Tên tài khoản: ", dye_light_yellow, false);
		getline(cin, Username);
		CFG.ReplaceKeyValue("username", Username);
	}

	if (!CFG.Exists("password"))
	{
		CONSOLE_PrintNoCRLF(u8"  Mật khẩu: ", dye_light_yellow, false);
		getline(cin, Password);
		CFG.ReplaceKeyValue("password", Password);
	}

	if (!CFG.Exists("channel"))
	{
		CONSOLE_PrintNoCRLF(u8"  Channel: ", dye_light_yellow, false);
		getline(cin, Channel);
		if (Channel.empty())
			Channel = "Warcraft 3 Frozen Throne";
		CFG.ReplaceKeyValue("channel", Channel);
	}

	CONSOLE_Print("", 0, false);
	CONSOLE_Print("  Welcome to GProxy.", 0, false);
	CONSOLE_Print("  Server: " + Server, 0, false);
	CONSOLE_Print("  Username: " + Username, 0, false);
	CONSOLE_Print("  Channel: " + Channel, 0, false);
	CONSOLE_Print("", 0, false);

	gCurses = true;
	
	// Start ImGui thread
	if (gCurses)
	{
		g_ImGuiThread = new std::thread(GuiThread);
		CONSOLE_Print("[GPROXY] ImGui GUI thread started", dye_light_green);
	}
	
	CONSOLE_Print("  Type /help at any time for help.", 0, false);
	CONSOLE_Print("", 0, false);
	
	gGProxy = new CGProxy( Hosts, UDPBindIP, UDPPort, GUIPort, UDPConsole, PublicGames, FilterGProxy, UDPPassword, UDPTrigger, !CDKeyTFT.empty( ), War3Path, CDKeyROC, CDKeyTFT, Server, Username, Password, Channel, War3Version, Port, EXEVersion, EXEVersionHash, PasswordHashType );

	if (War3Version == 28)
	{
		CreateThread(NULL, 0, InjectThread, NULL, 0, NULL);
	}

	while( 1 )
	{
		if( gGProxy->Update( 40000 ) )
			break;

		bool Quit = false;

		if (gGProxy->m_Quit)
			break;

		if( Quit || gGProxy->m_Quit)
			break;
	}


	PostQuitMessage(0);
	//t.join();

	// Cleanup ImGui thread
	if (g_ImGuiThread)
	{
		if (g_ImGuiThread->joinable())
			g_ImGuiThread->join();
		delete g_ImGuiThread;
		g_ImGuiThread = nullptr;
	}

	// shutdown gproxy

	CONSOLE_Print( "[GPROXY] shutting down" );
	delete gGProxy;
	gGProxy = NULL;

	// shutdown winsock

	CONSOLE_Print( "[GPROXY] shutting down winsock" );
	WSACleanup( );

	// restart the program
	if (gRestart)
	{
		_spawnl(_P_OVERLAY, argv[0], argv[0], nullptr);
	}

	// shutdown curses


	return 0;
}

//
// CGProxy
//

CGProxy :: CGProxy( string nHosts, string nUDPBindIP, uint16_t nUDPPort, uint16_t nGUIPort, bool nUDPConsole, bool nPublicGames, bool nFilterGProxy, string nUDPPassword, string nUDPTrigger, bool nTFT, string nWar3Path, string nCDKeyROC, string nCDKeyTFT, string nServer, string nUsername, string nPassword, string nChannel, uint32_t nWar3Version, uint16_t nPort, BYTEARRAY nEXEVersion, BYTEARRAY nEXEVersionHash, string nPasswordHashType )
{
	m_Version = "3.2 New - chỉnh sửa bởi Thái Sơn";
	m_LocalServer = new CTCPServer( );
	m_LocalSocket = NULL;
	m_RemoteSocket = new CTCPClient( );
	m_RemoteSocket->SetNoDelay( true );
	m_UDPSocket = new CUDPSocket( );
	m_Bonjour = new CBonjour();
	m_UDPSocket->SetBroadcastTarget( "127.0.0.1" );
	m_UDPCommandSocket = new CUDPServer();
	m_UDPCommandSocket->Bind(nUDPBindIP, nUDPPort);
	m_UDPConsole = nUDPConsole;
	m_UDPPassword = nUDPPassword;
	m_Console = true;
	m_GUIPort = nGUIPort;
	m_CMDPort = nUDPPort;
	CONSOLE_Print("[GPROXY] UDP Listening on port "+UTIL_ToString(nGUIPort));
	m_Quit = false;
	UTIL_ExtractStrings(nHosts, m_Hosts);
	m_PublicGames = nPublicGames;
	m_FilterGProxy = nFilterGProxy;
	m_GameProtocol = new CGameProtocol( this );
	m_GPSProtocol = new CGPSProtocol( );
	m_TotalPacketsReceivedFromLocal = 0;
	m_TotalPacketsReceivedFromRemote = 0;
	m_Exiting = false;
	m_TFT = nTFT;
	m_War3Path = nWar3Path;
	m_CDKeyROC = nCDKeyROC;
	m_CDKeyTFT = nCDKeyTFT;
	m_Server = nServer;
	m_Username = nUsername;
	m_Password = nPassword;
	m_Channel = nChannel;
	m_War3Version = nWar3Version;
	m_Port = nPort;
	m_LastConnectionAttemptTime = 0;
	m_LastRefreshTime = 0;
	m_RemoteServerPort = 0;
	m_GameIsReliable = false;
	m_GameStarted = false;
	m_LeaveGameSent = false;
	m_ActionReceived = false;
	m_Synchronized = true;
	m_ReconnectPort = 0;
	m_PID = 255;
	m_ChatPID = 255;
	m_ReconnectKey = 0;
	m_NumEmptyActions = 0;
	m_NumEmptyActionsUsed = 0;
	m_LastAckTime = 0;
	m_LastActionTime = 0;
	m_BNET = new CBNET( this, m_Server, string( ), 0, 0, m_CDKeyROC, m_CDKeyTFT, "USA", "United States", m_Username, m_Password, m_Channel, m_War3Version, nEXEVersion, nEXEVersionHash, nPasswordHashType, 200 );
	m_LocalServer->Listen( string( ), m_Port );
	if (m_PublicGames)
	{
		m_BNET->SetListPublicGames( true );
		CONSOLE_Print( "[BNET] listing of public games enabled" );
	}

	CONSOLE_Print( u8"[GPROXY] GProxy++ " + m_Version, dye_light_yellow);
}

CGProxy :: ~CGProxy( )
{
	for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
		m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );

	delete m_LocalServer;
	delete m_LocalSocket;
	delete m_RemoteSocket;
	delete m_UDPSocket;
	delete m_UDPCommandSocket;
	delete m_Bonjour;
	delete m_BNET;

	for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
		delete *i;

	delete m_GameProtocol;
	delete m_GPSProtocol;

	while( !m_LocalPackets.empty( ) )
	{
		delete m_LocalPackets.front( );
		m_LocalPackets.pop( );
	}

	while( !m_RemotePackets.empty( ) )
	{
		delete m_RemotePackets.front( );
		m_RemotePackets.pop( );
	}

	while( !m_PacketBuffer.empty( ) )
	{
		delete m_PacketBuffer.front( );
		m_PacketBuffer.pop( );
	}
}

bool CGProxy :: Update( long usecBlock )
{
	unsigned int NumFDs = 0;

	// take every socket we own and throw it in one giant select statement so we can block on all sockets

	int nfds = 0;
	fd_set fd;
	fd_set send_fd;
	FD_ZERO( &fd );
	FD_ZERO( &send_fd );

	// 1. the battle.net socket

	NumFDs += m_BNET->SetFD( &fd, &send_fd, &nfds );

	// 2. the local server

	m_LocalServer->SetFD( &fd, &send_fd, &nfds );
	NumFDs++;

	// 3. the local socket

	if( m_LocalSocket )
	{
		m_LocalSocket->SetFD( &fd, &send_fd, &nfds );
		NumFDs++;
	}

	// 4. the remote socket

	if( !m_RemoteSocket->HasError( ) && m_RemoteSocket->GetConnected( ) )
	{
		m_RemoteSocket->SetFD( &fd, &send_fd, &nfds );
		NumFDs++;
	}

	// 5. udp command receiving socket

	m_UDPCommandSocket->SetFD( &fd,  &send_fd, &nfds);
	// SetFD of the UDPServer does not return the number of sockets belonging to it as it's obviously one
	NumFDs++;

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = usecBlock;

	struct timeval send_tv;
	send_tv.tv_sec = 0;
	send_tv.tv_usec = 0;

#ifdef WIN32
	select( 1, &fd, NULL, NULL, &tv );
	select( 1, NULL, &send_fd, NULL, &send_tv );
#else
	select( nfds + 1, &fd, NULL, NULL, &tv );
	select( nfds + 1, NULL, &send_fd, NULL, &send_tv );
#endif

	if( NumFDs == 0 )
		MILLISLEEP( 50 );

	if( m_BNET->Update( &fd, &send_fd ) )
		return true;

	// UDP COMMANDSOCKET CODE
	sockaddr_in recvAddr;
	string udpcommand;
	m_UDPCommandSocket->RecvFrom( &fd, &recvAddr, &udpcommand);
	//	UDPChatAdd(recvAddr.sin_addr);
	if ( udpcommand.size() )
	{
		// default server to relay the message to
//		string udptarget = m_UDPCommandSpoofTarget;
		string udptarget = string();
		int pos;
		bool relayed = false;

		// special case, a udp command starting with || . ex: ||readwelcome
		if (udpcommand.substr(0,2)=="||")
		{
			UDPCommands(udpcommand.substr(1,udpcommand.length()-1));
		} 
		else
		{
			// has the user specified a specific target the command should be sent to?
			// looks for "<someaddress>" at the beginning of the received command,
			// sets the target accordingly and strips it from the command
			if ( udpcommand.find("<") == 0 && (pos=udpcommand.find(">")) != string::npos )
			{
				udptarget = udpcommand.substr(1, pos - 1);
				udpcommand.erase(0, pos + 1);
			}
			// we expect commands not to start with the command trigger because this is a commandsocket,
			// we only except commands and therefore know we received one and not some chatting
			// this way the user sending the command does not have to have knowledge of the commandtrigger
			// set in GHost's config file
			udpcommand = "/" + udpcommand;

			CONSOLE_Print("[UDPCMDSOCK] Relaying cmd [" + udpcommand + "]");
			gInputBuffer = udpcommand;
			Process_Command();
			relayed = true;
		}
	}

	//
	// accept new connections
	//

	CTCPSocket *NewSocket = m_LocalServer->Accept( &fd );

	if( NewSocket )
	{
		if( m_LocalSocket )
		{
			// someone's already connected, reject the new connection
			// we only allow one person to use the proxy at a time

			delete NewSocket;
		}
		else
		{
			CONSOLE_Print( "[GPROXY] local player connected" );
			m_LocalSocket = NewSocket;
			m_LocalSocket->SetNoDelay( true );
			m_TotalPacketsReceivedFromLocal = 0;
			m_TotalPacketsReceivedFromRemote = 0;
			m_GameIsReliable = false;
			m_GameStarted = false;
			m_LeaveGameSent = false;
			m_ActionReceived = false;
			m_Synchronized = true;
			m_ReconnectPort = 0;
			m_PID = 255;
			m_ChatPID = 255;
			m_ReconnectKey = 0;
			m_NumEmptyActions = 0;
			m_NumEmptyActionsUsed = 0;
			m_LastAckTime = 0;
			m_LastActionTime = 0;
			m_JoinedName.clear( );
			m_HostName.clear( );

			while( !m_PacketBuffer.empty( ) )
			{
				delete m_PacketBuffer.front( );
				m_PacketBuffer.pop( );
			}
		}
	}

	if( m_LocalSocket )
	{
		//
		// handle proxying (reconnecting, etc...)
		//

		if( m_LocalSocket->HasError( ) || !m_LocalSocket->GetConnected( ) )
		{
			CONSOLE_Print( "[GPROXY] local player disconnected" );

			if( m_BNET->GetInGame( ) )
				m_BNET->QueueEnterChat( );

			delete m_LocalSocket;
			m_LocalSocket = NULL;

			// ensure a leavegame message was sent, otherwise the server may wait for our reconnection which will never happen
			// if one hasn't been sent it's because Warcraft III exited abnormally

			if( m_GameIsReliable && !m_LeaveGameSent )
			{
				// note: we're not actually 100% ensuring the leavegame message is sent, we'd need to check that DoSend worked, etc...

				BYTEARRAY LeaveGame;
				LeaveGame.push_back( 0xF7 );
				LeaveGame.push_back( 0x21 );
				LeaveGame.push_back( 0x08 );
				LeaveGame.push_back( 0x00 );
				UTIL_AppendByteArray( LeaveGame, (uint32_t)PLAYERLEAVE_GPROXY, false );
				m_RemoteSocket->PutBytes( LeaveGame );
				m_RemoteSocket->DoSend( &send_fd );
			}

			m_RemoteSocket->Reset( );
			m_RemoteSocket->SetNoDelay( true );
			m_RemoteServerIP.clear( );
			m_RemoteServerPort = 0;
		}
		else
		{
			m_LocalSocket->DoRecv( &fd );
			ExtractLocalPackets( );
			ProcessLocalPackets( );

			if( !m_RemoteServerIP.empty( ) )
			{
				if( m_GameIsReliable && m_ActionReceived && GetTime( ) - m_LastActionTime >= 60 )
				{
					if( m_NumEmptyActionsUsed < m_NumEmptyActions )
					{
						SendEmptyAction( );
						m_NumEmptyActionsUsed++;
					}
					else
					{
						SendLocalChat( "GProxy++ ran out of time to reconnect, Warcraft III will disconnect soon." );
						CONSOLE_Print( "[GPROXY] ran out of time to reconnect" );
					}

					m_LastActionTime = GetTime( );
				}

				if( m_RemoteSocket->HasError( ) )
				{
					CONSOLE_Print( "[GPROXY] disconnected from remote server due to socket error" );

					if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 )
					{
						SendLocalChat( "You have been disconnected from the server due to a socket error." );
						uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

						if( GetTime( ) - m_LastActionTime > uint32_t( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
							TimeRemaining = 0;

						SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
						CONSOLE_Print( "[GPROXY] attempting to reconnect" );
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
						m_LastConnectionAttemptTime = GetTime( );
					}
					else
					{
						if( m_BNET->GetInGame( ) )
							m_BNET->QueueEnterChat( );

						m_LocalSocket->Disconnect( );
						delete m_LocalSocket;
						m_LocalSocket = NULL;
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteServerIP.clear( );
						m_RemoteServerPort = 0;
						return false;
					}
				}

				if( !m_RemoteSocket->GetConnecting( ) && !m_RemoteSocket->GetConnected( ) )
				{
					CONSOLE_Print( "[GPROXY] disconnected from remote server" );

					if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 )
					{
						SendLocalChat( "You have been disconnected from the server." );
						uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

						if( GetTime( ) - m_LastActionTime > uint32_t( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
							TimeRemaining = 0;

						SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
						CONSOLE_Print( "[GPROXY] attempting to reconnect" );
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
						m_LastConnectionAttemptTime = GetTime( );
					}
					else
					{
						if( m_BNET->GetInGame( ) )
							m_BNET->QueueEnterChat( );

						m_LocalSocket->Disconnect( );
						delete m_LocalSocket;
						m_LocalSocket = NULL;
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteServerIP.clear( );
						m_RemoteServerPort = 0;
						return false;
					}
				}

				if( m_RemoteSocket->GetConnected( ) )
				{
					if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 && GetTime( ) - m_RemoteSocket->GetLastRecv( ) >= 20 )
					{
						CONSOLE_Print( "[GPROXY] disconnected from remote server due to 20 second timeout" );
						SendLocalChat( "You have been timed out from the server." );
						uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

						if( GetTime( ) - m_LastActionTime > uint32_t( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
							TimeRemaining = 0;

						SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
						CONSOLE_Print( "[GPROXY] attempting to reconnect" );
						m_RemoteSocket->Reset( );
						m_RemoteSocket->SetNoDelay( true );
						m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
						m_LastConnectionAttemptTime = GetTime( );
					}
					else
					{
						m_RemoteSocket->DoRecv( &fd );
						ExtractRemotePackets( );
						ProcessRemotePackets( );

						if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 && GetTime( ) - m_LastAckTime >= 10 )
						{
							m_RemoteSocket->PutBytes( m_GPSProtocol->SEND_GPSC_ACK( m_TotalPacketsReceivedFromRemote ) );
							m_LastAckTime = GetTime( );
						}

						m_RemoteSocket->DoSend( &send_fd );
					}
				}

				if( m_RemoteSocket->GetConnecting( ) )
				{
					// we are currently attempting to connect

					if( m_RemoteSocket->CheckConnect( ) )
					{
						// the connection attempt completed

						if( m_GameIsReliable && m_ActionReceived )
						{
							// this is a reconnection, not a new connection
							// if the server accepts the reconnect request it will send a GPS_RECONNECT back requesting a certain number of packets

							SendLocalChat( "GProxy++ reconnected to the server!" );
							SendLocalChat( "==================================================" );
							CONSOLE_Print( "[GPROXY] reconnected to remote server" );

							// note: even though we reset the socket when we were disconnected, we haven't been careful to ensure we never queued any data in the meantime
							// therefore it's possible the socket could have data in the send buffer
							// this is bad because the server will expect us to send a GPS_RECONNECT message first
							// so we must clear the send buffer before we continue
							// note: we aren't losing data here, any important messages that need to be sent have been put in the packet buffer
							// they will be requested by the server if required

							m_RemoteSocket->ClearSendBuffer( );
							m_RemoteSocket->PutBytes( m_GPSProtocol->SEND_GPSC_RECONNECT( m_PID, m_ReconnectKey, m_TotalPacketsReceivedFromRemote ) );

							// we cannot permit any forwarding of local packets until the game is synchronized again
							// this will disable forwarding and will be reset when the synchronization is complete

							m_Synchronized = false;
						}
						else
							CONSOLE_Print( "[GPROXY] connected to remote server" );
					}
					else if( GetTime( ) - m_LastConnectionAttemptTime >= 10 )
					{
						// the connection attempt timed out (10 seconds)

						CONSOLE_Print( "[GPROXY] connect to remote server timed out" );

						if( m_GameIsReliable && m_ActionReceived && m_ReconnectPort > 0 )
						{
							uint32_t TimeRemaining = ( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 - ( GetTime( ) - m_LastActionTime );

							if( GetTime( ) - m_LastActionTime > uint32_t( m_NumEmptyActions - m_NumEmptyActionsUsed + 1 ) * 60 )
								TimeRemaining = 0;

							SendLocalChat( "GProxy++ is attempting to reconnect... (" + UTIL_ToString( TimeRemaining ) + " seconds remain)" );
							CONSOLE_Print( "[GPROXY] attempting to reconnect" );
							m_RemoteSocket->Reset( );
							m_RemoteSocket->SetNoDelay( true );
							m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_ReconnectPort );
							m_LastConnectionAttemptTime = GetTime( );
						}
						else
						{
							if( m_BNET->GetInGame( ) )
								m_BNET->QueueEnterChat( );

							m_LocalSocket->Disconnect( );
							delete m_LocalSocket;
							m_LocalSocket = NULL;
							m_RemoteSocket->Reset( );
							m_RemoteSocket->SetNoDelay( true );
							m_RemoteServerIP.clear( );
							m_RemoteServerPort = 0;
							return false;
						}
					}
				}
			}

			m_LocalSocket->DoSend( &send_fd );
		}
	}
	else
	{
		//
		// handle game listing
		//

		if( GetTime( ) - m_LastRefreshTime >= 2 )
		{
			for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); )
			{
				// expire games older than 60 seconds

				if( GetTime( ) - (*i)->GetReceivedTime( ) >= 60 )
				{
					// don't forget to remove it from the LAN list first

					m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );
					delete *i;
					i = m_Games.erase( i );
					continue;
				}

				BYTEARRAY MapGameType;
				UTIL_AppendByteArray( MapGameType, (*i)->GetGameType( ), false );
				UTIL_AppendByteArray( MapGameType, (*i)->GetParameter( ), false );
				BYTEARRAY MapFlags = UTIL_CreateByteArray( (*i)->GetMapFlags( ), false );
				BYTEARRAY MapWidth = UTIL_CreateByteArray( (*i)->GetMapWidth( ), false );
				BYTEARRAY MapHeight = UTIL_CreateByteArray( (*i)->GetMapHeight( ), false );
				string GameName = (*i)->GetGameName( );

				// colour reliable game names so they're easier to pick out of the list

				if( (*i)->GetMapWidth( ) == 1984 && (*i)->GetMapHeight( ) == 1984 )
				{
					GameName = "|cFF4080C0" + GameName;

					// unfortunately we have to truncate them
					// is this acceptable?

					if( GameName.size( ) > 31 )
						GameName = GameName.substr( 0, 31 );
				}

				if (m_War3Version >= 30)
				{
					if (m_Bonjour)
						m_Bonjour->Broadcast_Info(true, m_War3Version, MapGameType, MapFlags, MapWidth, MapHeight, GameName, (*i)->GetHostName(), (*i)->GetElapsedTime(), (*i)->GetMapPath(), (*i)->GetMapCRC(), 24, 24, m_Port, (*i)->GetUniqueGameID(), (*i)->GetUniqueGameID(), (*i)->GetMapHash());
				}
				else if (m_War3Version >= 29)
					m_UDPSocket->Broadcast(6112, m_GameProtocol->SEND_W3GS_GAMEINFO(true, m_War3Version, MapGameType, MapFlags, MapWidth, MapHeight, GameName, (*i)->GetHostName(), (*i)->GetElapsedTime(), (*i)->GetMapPath(), (*i)->GetMapCRC(), 24, 24, m_Port, (*i)->GetUniqueGameID(), (*i)->GetUniqueGameID()));
				else
					m_UDPSocket->Broadcast(6112, m_GameProtocol->SEND_W3GS_GAMEINFO(true, m_War3Version, MapGameType, MapFlags, MapWidth, MapHeight, GameName, (*i)->GetHostName(), (*i)->GetElapsedTime(), (*i)->GetMapPath(), (*i)->GetMapCRC(), 12, 12, m_Port, (*i)->GetUniqueGameID(), (*i)->GetUniqueGameID()));

				i++;
			}

			m_LastRefreshTime = GetTime( );
		}
	}

	return m_Exiting;
}

void CGProxy :: ExtractLocalPackets( )
{
	if( !m_LocalSocket )
		return;

	string *RecvBuffer = m_LocalSocket->GetBytes( );
	BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

	// a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

	while( Bytes.size( ) >= 4 )
	{
		// byte 0 is always 247

		if( Bytes[0] == W3GS_HEADER_CONSTANT )
		{
			// bytes 2 and 3 contain the length of the packet

			uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

			if( Length >= 4 )
			{
				if( Bytes.size( ) >= Length )
				{
					// we have to do a little bit of packet processing here
					// this is because we don't want to forward any chat messages that start with a "/" as these may be forwarded to battle.net instead
					// in fact we want to pretend they were never even received from the proxy's perspective

					bool Forward = true;
					BYTEARRAY Data = BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length );

					if( Bytes[1] == CGameProtocol :: W3GS_CHAT_TO_HOST )
					{
						if( Data.size( ) >= 5 )
						{
							unsigned int i = 5;
							unsigned char Total = Data[4];

							if( Total > 0 && Data.size( ) >= i + Total )
							{
								i += Total;
								unsigned char Flag = Data[i + 1];
								i += 2;

								string MessageString;

								if( Flag == 16 && Data.size( ) >= i + 1 )
								{
									BYTEARRAY Message = UTIL_ExtractCString( Data, i );
									MessageString = string( Message.begin( ), Message.end( ) );
								}
								else if( Flag == 32 && Data.size( ) >= i + 5 )
								{
									BYTEARRAY Message = UTIL_ExtractCString( Data, i + 4 );
									MessageString = string( Message.begin( ), Message.end( ) );
								}

								string Command = MessageString;
								transform( Command.begin( ), Command.end( ), Command.begin( ), (int(*)(int))tolower );

								if( Command.size( ) >= 1 && Command.substr( 0, 1 ) == "/" )
								{
									Forward = false;

									if( Command.size( ) >= 5 && Command.substr( 0, 4 ) == "/re " )
									{
										if( m_BNET->GetLoggedIn( ) )
										{
											if( !m_BNET->GetReplyTarget( ).empty( ) )
											{
												m_BNET->QueueChatCommand( MessageString.substr( 4 ), m_BNET->GetReplyTarget( ), true );
												SendLocalChat( "Whispered to " + m_BNET->GetReplyTarget( ) + ": " + MessageString.substr( 4 ) );
											}
											else
												SendLocalChat( "Nobody has whispered you yet." );
										}
										else
											SendLocalChat( "You are not connected to battle.net." );
									}
									else if( Command == "/sc" || Command == "/spoof" || Command == "/spoofcheck" || Command == "/spoof check" )
									{
										if( m_BNET->GetLoggedIn( ) )
										{
											if( !m_GameStarted )
											{
												m_BNET->QueueChatCommand( "spoofcheck", m_HostName, true );
												SendLocalChat( "Whispered to " + m_HostName + ": spoofcheck" );
											}
											else
												SendLocalChat( "The game has already started." );
										}
										else
											SendLocalChat( "You are not connected to battle.net." );
									}
									else if( Command == "/status" )
									{
										if( m_LocalSocket )
										{
											if( m_GameIsReliable && m_ReconnectPort > 0 )
												SendLocalChat( "GProxy++ disconnect protection: Enabled" );
											else
												SendLocalChat( "GProxy++ disconnect protection: Disabled" );

											if( m_BNET->GetLoggedIn( ) )
												SendLocalChat( "battle.net: Connected" );
											else
												SendLocalChat( "battle.net: Disconnected" );
										}
									}
									else if( Command.size( ) >= 4 && Command.substr( 0, 3 ) == "/w " )
									{
										if( m_BNET->GetLoggedIn( ) )
										{
											// todotodo: fix me

											m_BNET->QueueChatCommand( MessageString );
											// SendLocalChat( "Whispered to ???: ???" );
										}
										else
											SendLocalChat( "You are not connected to battle.net." );
									}
								}
							}
						}
					}

					if( Forward )
					{
						m_LocalPackets.push( new CCommandPacket( W3GS_HEADER_CONSTANT, Bytes[1], Data ) );
						m_PacketBuffer.push( new CCommandPacket( W3GS_HEADER_CONSTANT, Bytes[1], Data ) );
						m_TotalPacketsReceivedFromLocal++;
					}

					*RecvBuffer = RecvBuffer->substr( Length );
					Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
				}
				else
					return;
			}
			else
			{
				CONSOLE_Print( "[GPROXY] received invalid packet from local player (bad length)" );
				m_Exiting = true;
				return;
			}
		}
		else
		{
			CONSOLE_Print( "[GPROXY] received invalid packet from local player (bad header constant)" );
			m_Exiting = true;
			return;
		}
	}
}

void CGProxy :: ProcessLocalPackets( )
{
	if( !m_LocalSocket )
		return;

	while( !m_LocalPackets.empty( ) )
	{
		CCommandPacket *Packet = m_LocalPackets.front( );
		m_LocalPackets.pop( );
		BYTEARRAY Data = Packet->GetData( );

		if( Packet->GetPacketType( ) == W3GS_HEADER_CONSTANT )
		{
			if( Packet->GetID( ) == CGameProtocol :: W3GS_REQJOIN )
			{
				if( Data.size( ) >= 20 )
				{
					// parse

					uint32_t HostCounter = UTIL_ByteArrayToUInt32( Data, false, 4 );
					uint32_t EntryKey = UTIL_ByteArrayToUInt32( Data, false, 8 );
					unsigned char Unknown = Data[12];
					uint16_t ListenPort = UTIL_ByteArrayToUInt16( Data, false, 13 );
					uint32_t PeerKey = UTIL_ByteArrayToUInt32( Data, false, 15 );
					BYTEARRAY Name = UTIL_ExtractCString( Data, 19 );
					string NameString = string( Name.begin( ), Name.end( ) );
					BYTEARRAY Remainder = BYTEARRAY( Data.begin( ) + Name.size( ) + 20, Data.end( ) );

					//CONSOLE_Print("[GPROXY] Remainder -> " + to_string(Remainder.size()));

					if ((Remainder.size() == 18 && War3Version <= 28) || (Remainder.size() == 19 && War3Version >= 29))
					{
						// lookup the game in the main list

						bool GameFound = false;

						for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
						{
							if( (*i)->GetUniqueGameID( ) == EntryKey )
							{
								CONSOLE_Print( "[GPROXY] local player requested game name [" + (*i)->GetGameName( ) + "]" );

								/*if( NameString != m_Username )
									CONSOLE_Print( "[GPROXY] using battle.net name [" + m_Username + "] instead of requested name [" + NameString + "]" );*/

								CONSOLE_Print( "[GPROXY] connecting to remote server [" + (*i)->GetIPString( ) + "] on port " + UTIL_ToString( (*i)->GetPort( ) ) );
								m_RemoteServerIP = (*i)->GetIPString( );
								m_RemoteServerPort = (*i)->GetPort( );
								m_RemoteSocket->Reset( );
								m_RemoteSocket->SetNoDelay( true );
								m_RemoteSocket->Connect( string( ), m_RemoteServerIP, m_RemoteServerPort );
								m_LastConnectionAttemptTime = GetTime( );
								m_GameIsReliable = ( (*i)->GetMapWidth( ) == 1984 && (*i)->GetMapHeight( ) == 1984 );
								m_GameStarted = false;

								// rewrite packet

								BYTEARRAY DataRewritten;
								DataRewritten.push_back( W3GS_HEADER_CONSTANT );
								DataRewritten.push_back( Packet->GetID( ) );
								DataRewritten.push_back( 0 );
								DataRewritten.push_back( 0 );
								UTIL_AppendByteArray( DataRewritten, (*i)->GetHostCounter( ), false );
								UTIL_AppendByteArray( DataRewritten, (uint32_t)0, false );
								DataRewritten.push_back( Unknown );
								UTIL_AppendByteArray( DataRewritten, ListenPort, false );
								UTIL_AppendByteArray( DataRewritten, PeerKey, false );
								UTIL_AppendByteArray( DataRewritten, m_Username );
								UTIL_AppendByteArrayFast( DataRewritten, Remainder );
								BYTEARRAY LengthBytes;
								LengthBytes = UTIL_CreateByteArray( (uint16_t)DataRewritten.size( ), false );
								DataRewritten[2] = LengthBytes[0];
								DataRewritten[3] = LengthBytes[1];
								Data = DataRewritten;

								// tell battle.net we're joining a game (for automatic spoof checking)

								m_BNET->QueueJoinGame( (*i)->GetGameName( ) );

								// save the hostname for later (for manual spoof checking)
									
								//m_JoinedName = NameString;
								m_JoinedName = m_Username;
								m_HostName = (*i)->GetHostName( );
								GameFound = true;
								break;
							}
						}

						if( !GameFound )
						{
							CONSOLE_Print( "[GPROXY] local player requested unknown game (expired?)" );
							m_LocalSocket->Disconnect( );
						}
					}
					else
						CONSOLE_Print( "[GPROXY] received invalid join request from local player (invalid remainder)" );
				}
				else
					CONSOLE_Print( "[GPROXY] received invalid join request from local player (too short)" );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_LEAVEGAME )
			{
				m_LeaveGameSent = true;
				m_LocalSocket->Disconnect( );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_CHAT_TO_HOST )
			{
				// handled in ExtractLocalPackets (yes, it's ugly)
			}
		}

		// warning: do not forward any data if we are not synchronized (e.g. we are reconnecting and resynchronizing)
		// any data not forwarded here will be cached in the packet buffer and sent later so all is well

		if( m_RemoteSocket && m_Synchronized )
			m_RemoteSocket->PutBytes( Data );

		delete Packet;
	}
}

void CGProxy :: ExtractRemotePackets( )
{
	string *RecvBuffer = m_RemoteSocket->GetBytes( );
	BYTEARRAY Bytes = UTIL_CreateByteArray( (unsigned char *)RecvBuffer->c_str( ), RecvBuffer->size( ) );

	// a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

	while( Bytes.size( ) >= 4 )
	{
		if( Bytes[0] == W3GS_HEADER_CONSTANT || Bytes[0] == GPS_HEADER_CONSTANT )
		{
			// bytes 2 and 3 contain the length of the packet

			uint16_t Length = UTIL_ByteArrayToUInt16( Bytes, false, 2 );

			if( Length >= 4 )
			{
				if( Bytes.size( ) >= Length )
				{
					m_RemotePackets.push( new CCommandPacket( Bytes[0], Bytes[1], BYTEARRAY( Bytes.begin( ), Bytes.begin( ) + Length ) ) );

					if( Bytes[0] == W3GS_HEADER_CONSTANT )
						m_TotalPacketsReceivedFromRemote++;

					*RecvBuffer = RecvBuffer->substr( Length );
					Bytes = BYTEARRAY( Bytes.begin( ) + Length, Bytes.end( ) );
				}
				else
					return;
			}
			else
			{
				CONSOLE_Print( "[GPROXY] received invalid packet from remote server (bad length)" );
				m_Exiting = true;
				return;
			}
		}
		else
		{
			CONSOLE_Print( "[GPROXY] received invalid packet from remote server (bad header constant)" );
			m_Exiting = true;
			return;
		}
	}
}

void CGProxy :: ProcessRemotePackets( )
{
	if( !m_LocalSocket || !m_RemoteSocket )
		return;

	while( !m_RemotePackets.empty( ) )
	{
		CCommandPacket *Packet = m_RemotePackets.front( );
		m_RemotePackets.pop( );

		if( Packet->GetPacketType( ) == W3GS_HEADER_CONSTANT )
		{
			if( Packet->GetID( ) == CGameProtocol :: W3GS_SLOTINFOJOIN )
			{
				BYTEARRAY Data = Packet->GetData( );

				if( Data.size( ) >= 6 )
				{
					uint16_t SlotInfoSize = UTIL_ByteArrayToUInt16( Data, false, 4 );

					if( Data.size( ) >= uint16_t(7 + SlotInfoSize) )
						m_ChatPID = Data[6 + SlotInfoSize];
				}

				// send a GPS_INIT packet
				// if the server doesn't recognize it (e.g. it isn't GHost++) we should be kicked

				CONSOLE_Print( "[GPROXY] join request accepted by remote server" );

				if( m_GameIsReliable )
				{
					CONSOLE_Print( "[GPROXY] detected reliable game, starting GPS handshake" );
					m_RemoteSocket->PutBytes( m_GPSProtocol->SEND_GPSC_INIT( 1 ) );
				}
				else
					CONSOLE_Print( "[GPROXY] detected standard game, disconnect protection disabled" );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_COUNTDOWN_END )
			{
				if( m_GameIsReliable && m_ReconnectPort > 0 )
					CONSOLE_Print( "[GPROXY] game started, disconnect protection enabled" );
				else
				{
					if( m_GameIsReliable )
						CONSOLE_Print( "[GPROXY] game started but GPS handshake not complete, disconnect protection disabled" );
					else
						CONSOLE_Print( "[GPROXY] game started" );
				}

				m_GameStarted = true;
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_INCOMING_ACTION )
			{
				if( m_GameIsReliable )
				{
					// we received a game update which means we can reset the number of empty actions we have to work with
					// we also must send any remaining empty actions now
					// note: the lag screen can't be up right now otherwise the server made a big mistake, so we don't need to check for it

					BYTEARRAY EmptyAction;
					EmptyAction.push_back( 0xF7 );
					EmptyAction.push_back( 0x0C );
					EmptyAction.push_back( 0x06 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );

					for( unsigned char i = m_NumEmptyActionsUsed; i < m_NumEmptyActions; i++ )
						m_LocalSocket->PutBytes( EmptyAction );

					m_NumEmptyActionsUsed = 0;
				}

				m_ActionReceived = true;
				m_LastActionTime = GetTime( );
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_START_LAG )
			{
				if( m_GameIsReliable )
				{
					BYTEARRAY Data = Packet->GetData( );

					if( Data.size( ) >= 5 )
					{
						unsigned char NumLaggers = Data[4];

						if( Data.size( ) == 5 + NumLaggers * 5 )
						{
							for( unsigned char i = 0; i < NumLaggers; i++ )
							{
								bool LaggerFound = false;

								for( vector<unsigned char> :: iterator j = m_Laggers.begin( ); j != m_Laggers.end( ); j++ )
								{
									if( *j == Data[5 + i * 5] )
										LaggerFound = true;
								}

								if( LaggerFound )
									CONSOLE_Print( "[GPROXY] warning - received start_lag on known lagger" );
								else
									m_Laggers.push_back( Data[5 + i * 5] );
							}
						}
						else
							CONSOLE_Print( "[GPROXY] warning - unhandled start_lag (2)" );
					}
					else
						CONSOLE_Print( "[GPROXY] warning - unhandled start_lag (1)" );
				}
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_STOP_LAG )
			{
				if( m_GameIsReliable )
				{
					BYTEARRAY Data = Packet->GetData( );

					if( Data.size( ) == 9 )
					{
						bool LaggerFound = false;

						for( vector<unsigned char> :: iterator i = m_Laggers.begin( ); i != m_Laggers.end( ); )
						{
							if( *i == Data[4] )
							{
								i = m_Laggers.erase( i );
								LaggerFound = true;
							}
							else
								i++;
						}

						if( !LaggerFound )
							CONSOLE_Print( "[GPROXY] warning - received stop_lag on unknown lagger" );
					}
					else
						CONSOLE_Print( "[GPROXY] warning - unhandled stop_lag" );
				}
			}
			else if( Packet->GetID( ) == CGameProtocol :: W3GS_INCOMING_ACTION2 )
			{
				if( m_GameIsReliable )
				{
					// we received a fractured game update which means we cannot use any empty actions until we receive the subsequent game update
					// we also must send any remaining empty actions now
					// note: this means if we get disconnected right now we can't use any of our buffer time, which would be very unlucky
					// it still gives us 60 seconds total to reconnect though
					// note: the lag screen can't be up right now otherwise the server made a big mistake, so we don't need to check for it

					BYTEARRAY EmptyAction;
					EmptyAction.push_back( 0xF7 );
					EmptyAction.push_back( 0x0C );
					EmptyAction.push_back( 0x06 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );
					EmptyAction.push_back( 0x00 );

					for( unsigned char i = m_NumEmptyActionsUsed; i < m_NumEmptyActions; i++ )
						m_LocalSocket->PutBytes( EmptyAction );

					m_NumEmptyActionsUsed = m_NumEmptyActions;
				}
			}

			// forward the data

			m_LocalSocket->PutBytes( Packet->GetData( ) );

			// we have to wait until now to send the status message since otherwise the slotinfojoin itself wouldn't have been forwarded

			if( Packet->GetID( ) == CGameProtocol :: W3GS_SLOTINFOJOIN )
			{
				if( m_JoinedName != m_Username )
					SendLocalChat( "Using battle.net name \"" + m_Username + "\" instead of LAN name \"" + m_JoinedName + "\"." );

				if( m_GameIsReliable )
					SendLocalChat( "This is a reliable game. Requesting GProxy++ disconnect protection from server..." );
				else
					SendLocalChat( "This is an unreliable game. GProxy++ disconnect protection is disabled." );
			}
		}
		else if( Packet->GetPacketType( ) == GPS_HEADER_CONSTANT )
		{
			if( m_GameIsReliable )
			{
				BYTEARRAY Data = Packet->GetData( );

				if( Packet->GetID( ) == CGPSProtocol :: GPS_INIT && Data.size( ) == 12 )
				{
					m_ReconnectPort = UTIL_ByteArrayToUInt16( Data, false, 4 );
					m_PID = Data[6];
					m_ReconnectKey = UTIL_ByteArrayToUInt32( Data, false, 7 );
					m_NumEmptyActions = Data[11];
					SendLocalChat( "GProxy++ disconnect protection is ready (" + UTIL_ToString( ( m_NumEmptyActions + 1 ) * 60 ) + " second buffer)." );
					CONSOLE_Print( "[GPROXY] handshake complete, disconnect protection ready (" + UTIL_ToString( ( m_NumEmptyActions + 1 ) * 60 ) + " second buffer)" );
				}
				else if( Packet->GetID( ) == CGPSProtocol :: GPS_RECONNECT && Data.size( ) == 8 )
				{
					uint32_t LastPacket = UTIL_ByteArrayToUInt32( Data, false, 4 );
					uint32_t PacketsAlreadyUnqueued = m_TotalPacketsReceivedFromLocal - m_PacketBuffer.size( );

					if( LastPacket > PacketsAlreadyUnqueued )
					{
						uint32_t PacketsToUnqueue = LastPacket - PacketsAlreadyUnqueued;

						if( PacketsToUnqueue > m_PacketBuffer.size( ) )
							PacketsToUnqueue = m_PacketBuffer.size( );
	
						while( PacketsToUnqueue > 0 )
						{
							delete m_PacketBuffer.front( );
							m_PacketBuffer.pop( );
							PacketsToUnqueue--;
						}
					}

					// send remaining packets from buffer, preserve buffer
					// note: any packets in m_LocalPackets are still sitting at the end of this buffer because they haven't been processed yet
					// therefore we must check for duplicates otherwise we might (will) cause a desync

					queue<CCommandPacket *> TempBuffer;

					while( !m_PacketBuffer.empty( ) )
					{
						if( m_PacketBuffer.size( ) > m_LocalPackets.size( ) )
							m_RemoteSocket->PutBytes( m_PacketBuffer.front( )->GetData( ) );

						TempBuffer.push( m_PacketBuffer.front( ) );
						m_PacketBuffer.pop( );
					}

					m_PacketBuffer = TempBuffer;

					// we can resume forwarding local packets again
					// doing so prior to this point could result in an out-of-order stream which would probably cause a desync

					m_Synchronized = true;
				}
				else if( Packet->GetID( ) == CGPSProtocol :: GPS_ACK && Data.size( ) == 8 )
				{
					uint32_t LastPacket = UTIL_ByteArrayToUInt32( Data, false, 4 );
					uint32_t PacketsAlreadyUnqueued = m_TotalPacketsReceivedFromLocal - m_PacketBuffer.size( );

					if (LastPacket > PacketsAlreadyUnqueued)
					{
						uint32_t PacketsToUnqueue = LastPacket - PacketsAlreadyUnqueued;

						if( PacketsToUnqueue > m_PacketBuffer.size( ) )
							PacketsToUnqueue = m_PacketBuffer.size( );

						while( PacketsToUnqueue > 0 )
						{
							delete m_PacketBuffer.front( );
							m_PacketBuffer.pop( );
							PacketsToUnqueue--;
						}
					}
				}
				else if( Packet->GetID( ) == CGPSProtocol :: GPS_REJECT && Data.size( ) == 8 )
				{
					uint32_t Reason = UTIL_ByteArrayToUInt32( Data, false, 4 );

					if( Reason == REJECTGPS_INVALID )
						CONSOLE_Print( "[GPROXY] rejected by remote server: invalid data" );
					else if( Reason == REJECTGPS_NOTFOUND )
						CONSOLE_Print( "[GPROXY] rejected by remote server: player not found in any running games" );

					m_LocalSocket->Disconnect( );
				}
			}
		}

		delete Packet;
	}
}

bool CGProxy :: AddGame( CIncomingGameHost *game )
{
	// check for duplicates and rehosted games

	bool DuplicateFound = false;
	bool AllowedHost = true;
	if (m_Hosts.size()>0)
		AllowedHost = false;

	uint32_t OldestReceivedTime = GetTime( );

	for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
	{
		if( game->GetIP( ) == (*i)->GetIP( ) && game->GetPort( ) == (*i)->GetPort( ) )
		{
			// duplicate or rehosted game, delete the old one and add the new one
			// don't forget to remove the old one from the LAN list first

			m_UDPSocket->Broadcast( 6112, m_GameProtocol->SEND_W3GS_DECREATEGAME( (*i)->GetUniqueGameID( ) ) );
			delete *i;
			*i = game;
			DuplicateFound = true;
			break;
		}

		if (game->GetReceivedTime() < OldestReceivedTime) 
		{
			vector<string> games = m_BNET->GetSearchGameName();
			for (vector<string> ::iterator i = games.begin(); i != games.end(); i++) 
			{
				std::string Game = (*i);
				std::string GameName = Game.substr(0, Game.find_first_of(":"));
				if (GameName != game->GetGameName()) 
				{
					OldestReceivedTime = game->GetReceivedTime();
				}
			}
		}
	}

	string n = game->GetHostName();
	transform( n.begin( ), n.end( ), n.begin( ), (int(*)(int))tolower );

	for (uint32_t i = 0; i < m_Hosts.size(); i++)
	{
		if (m_Hosts[i] == n)
			AllowedHost = true;
	}

	if (m_FilterGProxy)
		AllowedHost = false;

	if( !DuplicateFound && AllowedHost )
		m_Games.push_back( game );

	if( m_Games.size( ) > 20 )
	{
		for( vector<CIncomingGameHost *> :: iterator i = m_Games.begin( ); i != m_Games.end( ); i++ )
		{
			if (game->GetReceivedTime() == OldestReceivedTime)
			{
				vector<string> games = m_BNET->GetSearchGameName();
				for (vector<string> ::iterator j = games.begin(); j != games.end(); j++) 
				{
					std::string Game = (*j);
					std::string GameName = Game.substr(0, Game.find_first_of(":"));
					if (GameName != game->GetGameName()) 
					{
						m_UDPSocket->Broadcast(6112, m_GameProtocol->SEND_W3GS_DECREATEGAME((*i)->GetUniqueGameID()));
						delete* i;
						m_Games.erase(i);
						break;
					}
				}
			}
		}
	}

	return !DuplicateFound;
}

void CGProxy :: SendLocalChat( string message )
{
	if( m_LocalSocket )
	{
		if( m_GameStarted )
		{
			if( message.size( ) > 127 )
				message = message.substr( 0, 127 );

			m_LocalSocket->PutBytes( m_GameProtocol->SEND_W3GS_CHAT_FROM_HOST( m_ChatPID, UTIL_CreateByteArray( m_ChatPID ), 32, UTIL_CreateByteArray( (uint32_t)0, false ), message ) );
		}
		else
		{
			if( message.size( ) > 254 )
				message = message.substr( 0, 254 );

			m_LocalSocket->PutBytes( m_GameProtocol->SEND_W3GS_CHAT_FROM_HOST( m_ChatPID, UTIL_CreateByteArray( m_ChatPID ), 16, BYTEARRAY( ), message ) );
		}
	}
}

void CGProxy :: SendEmptyAction( )
{
	// we can't send any empty actions while the lag screen is up
	// so we keep track of who the lag screen is currently showing (if anyone) and we tear it down, send the empty action, and put it back up

	for( vector<unsigned char> :: iterator i = m_Laggers.begin( ); i != m_Laggers.end( ); i++ )
	{
		BYTEARRAY StopLag;
		StopLag.push_back( 0xF7 );
		StopLag.push_back( 0x11 );
		StopLag.push_back( 0x09 );
		StopLag.push_back( 0 );
		StopLag.push_back( *i );
		UTIL_AppendByteArray( StopLag, (uint32_t)60000, false );
		m_LocalSocket->PutBytes( StopLag );
	}

	BYTEARRAY EmptyAction;
	EmptyAction.push_back( 0xF7 );
	EmptyAction.push_back( 0x0C );
	EmptyAction.push_back( 0x06 );
	EmptyAction.push_back( 0x00 );
	EmptyAction.push_back( 0x00 );
	EmptyAction.push_back( 0x00 );
	m_LocalSocket->PutBytes( EmptyAction );

	if( !m_Laggers.empty( ) )
	{
		BYTEARRAY StartLag;
		StartLag.push_back( 0xF7 );
		StartLag.push_back( 0x10 );
		StartLag.push_back( 0 );
		StartLag.push_back( 0 );
		StartLag.push_back( (unsigned char)m_Laggers.size( ) );

		for( vector<unsigned char> :: iterator i = m_Laggers.begin( ); i != m_Laggers.end( ); i++ )
		{
			// using a lag time of 60000 ms means the counter will start at zero
			// hopefully warcraft 3 doesn't care about wild variations in the lag time in subsequent packets

			StartLag.push_back( *i );
			UTIL_AppendByteArray( StartLag, (uint32_t)60000, false );
		}

		BYTEARRAY LengthBytes;
		LengthBytes = UTIL_CreateByteArray( (uint16_t)StartLag.size( ), false );
		StartLag[2] = LengthBytes[0];
		StartLag[3] = LengthBytes[1];
		m_LocalSocket->PutBytes( StartLag );
	}
}

void CGProxy :: UDPChatDel(string ip)
{
	for( vector<string> :: iterator i = m_UDPUsers.begin( ); i != m_UDPUsers.end( ); i++ )
	{
		if( (*i) == ip )
		{
			CONSOLE_Print("[UDPCMDSOCK] User "+ ip + " removed from UDP user list");
			m_UDPUsers.erase(i);
			return;
		}
	}
}

bool CGProxy :: UDPChatAuth(string ip)
{
	if (ip=="127.0.0.1")
		return true;

	for( vector<string> :: iterator i = m_UDPUsers.begin( ); i != m_UDPUsers.end( ); i++ )
	{
		if( (*i) == ip )
		{
			return true;
		}
	}
	return false;
}

void CGProxy :: UDPChatAdd(string ip)
{
	// check that the ip is not already in the list
	for( vector<string> :: iterator i = gGProxy->m_UDPUsers.begin( ); i != gGProxy->m_UDPUsers.end( ); i++ )
	{
		if( (*i) == ip )
		{
			return;
		}
	}
	CONSOLE_Print("[UDPCMDSOCK] User "+ ip + " added to UDP user list");
	m_UDPUsers.push_back( ip );
}

void CGProxy :: UDPChatSend(BYTEARRAY b)
{
	if (m_UDPUsers.size()>0)
	{
		string ip;
		for( vector<string> :: iterator i = m_UDPUsers.begin( ); i != m_UDPUsers.end( ); i++ )
		{
			ip = (*i);			
			m_UDPSocket->SendTo(ip,6112,b);
		}
	}

	if (m_IPUsers.size()>0)
	{
		string ip;
		for( vector<string> :: iterator i = m_IPUsers.begin( ); i != m_IPUsers.end( ); i++ )
		{
			ip = (*i);			
			m_UDPSocket->SendTo(ip,6112,b);
		}
	}
}

void CGProxy :: UDPChatSend(string s)
{
	m_inconsole = true;
	//	CONSOLE_Print("[UDP] "+s);
	m_inconsole = false;
	string ip;
	//	u_short port;
	BYTEARRAY b;
	char *c = new char[s.length()+2];
	strncpy(c,s.c_str(), s.length());
	c[s.length()]=0;
	b=UTIL_CreateByteArray(c,s.length());
	if (m_UDPUsers.size()>0)
	{
		for( vector<string> :: iterator i = m_UDPUsers.begin( ); i != m_UDPUsers.end( ); i++ )
		{
			ip = (*i);			
			m_UDPSocket->SendTo(ip,m_GUIPort,b);
		}
	}
	m_UDPSocket->SendTo("127.0.0.1",m_GUIPort,b);
}

void CGProxy :: UDPChatSendBack(string s)
{
	m_inconsole = true;
	//	CONSOLE_Print("[UDP] "+s);
	m_inconsole = false;
	if (m_LastIp=="") return;
	BYTEARRAY b;
	char *c = new char[s.length()+2];
	strncpy(c,s.c_str(), s.length());
	c[s.length()]=0;
	b=UTIL_CreateByteArray(c,s.length());
	m_UDPSocket->SendTo(m_LastIp,m_GUIPort,b);
}

void CGProxy :: UDPCommands( string Message )
{
	string IP;
	string Command;
	string Payload;
	string :: size_type CommandStart = Message.find( " " );

	//	CONSOLE_Print( Message );

	if( CommandStart != string :: npos )
	{
		IP = Message.substr( 1, CommandStart-1 );
		Message = Message.substr( CommandStart + 1 );
	}
	else
		Message = Message.substr( 1 );

	m_LastIp = IP;

	string :: size_type PayloadStart = Message.find( " " );

	if( PayloadStart != string :: npos )
	{
		Command = Message.substr( 0, PayloadStart-0 );
		Payload = Message.substr( PayloadStart + 1 );
	}
	else
		Command = Message.substr( 0 );

	transform( Command.begin( ), Command.end( ), Command.begin( ), (int(*)(int))tolower );

	//	CONSOLE_Print( "[GPROXY] received UDP command [" + Command + "] with payload [" + Payload + "]"+" from IP ["+IP+"]" );
	if (Command == "connect" && !Payload.empty())
	{
		if (m_UDPPassword!="")
			if (Payload!=m_UDPPassword)
			{
				UDPChatSendBack("|invalidpassword");
				return;
			}
			UDPChatAdd(IP);
			UDPChatSend("|connected "+IP);
			m_BNET->SendGetFriendsList();
	}

	if (!UDPChatAuth(IP)) 
		return;

	//	CONSOLE_Print( "[GHOST] received UDP command [" + Command + "] with payload [" + Payload + "]"+" from IP ["+IP+"]" );

	if (Command == "ping")
	{
		UDPChatSendBack("|pong");
	}

	if (Command == "rcfg")
	{
		ReloadConfig();
	}

	if (Command == "disconnect" && !Payload.empty())
	{
		UDPChatDel(Payload);
		UDPChatSend("|disconnected "+Payload);
	}

	if (Command == "getchannel")
	{
		string Users="";

		if (gChannelUsers.size())
		{

			for( vector<string> :: iterator i = gChannelUsers.begin( ); i != gChannelUsers.end( ); i++ )
			{
				Users += (*i)+",";
			}
			UDPChatSendBack("|channelusers "+Users);
		}
	}

	if (Command == "say" && !Payload.empty())
	{
		m_BNET->QueueChatCommand(Payload);
	}

	if (Command == "updatefriends")
	{
		m_BNET->SendGetFriendsList();		
	}

	if (Command == "getfriends")
	{
		string friends = string();
			if (m_BNET->GetFriends()->size( )>0)
			{
				for( vector<CIncomingFriendList *> :: iterator g = m_BNET->GetFriends()->begin( ); g != m_BNET->GetFriends()->end( ); g++)
				{
					friends = friends + (*g)->GetAccount()+","+UTIL_ToString((*g)->GetStatus())+","+UTIL_ToString((*g)->GetArea())+",";				
				}
				UDPChatSendBack("|friends "+friends);
			}
	}
}

void CGProxy :: ReloadConfig ()
{

}