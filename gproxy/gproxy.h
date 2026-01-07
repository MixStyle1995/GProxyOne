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

#ifndef GPROXY_H
#define GPROXY_H

// standard integer sizes for 64 bit compatibility

#ifdef WIN32
 #include <stdint.h>
#endif

// STL

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include <mutex>

using namespace std;

typedef vector<unsigned char> BYTEARRAY;

#define dye_black			0
#define dye_blue			1
#define dye_green			2
#define dye_aqua			3
#define dye_red				4
#define dye_purple			5
#define dye_yellow			6
#define dye_white			7
#define dye_grey			8
#define dye_light_blue		9
#define dye_light_green		10
#define dye_light_aqua		11
#define dye_light_red		12
#define dye_light_purple	13
#define dye_light_yellow	14
#define dye_bright_white	15

#include "config.h"

template<typename T>
void removeSubstrs(basic_string<T>& s, const basic_string<T>& p, const basic_string<T>& r)
{
	basic_string<T>::size_type n = p.length();
	for (basic_string<T>::size_type i = s.find(p); i != basic_string<T>::npos; i = s.find(p))
		s.replace(i, n, r);
}

template<typename T>
void removeSubstrs(basic_string<T>& s, const basic_string<T>& p)
{
	basic_string<T>::size_type n = p.length();
	for (basic_string<T>::size_type i = s.find(p); i != basic_string<T>::npos; i = s.find(p))
		s.erase(i, n);
}

// time

uint32_t GetTime( );		// seconds
uint32_t GetTicks( );		// milliseconds

#ifdef WIN32
 #define MILLISLEEP( x ) Sleep( x )
#else
 #define MILLISLEEP( x ) usleep( ( x ) * 1000 )
#endif

struct ColoredSegment
{
	wstring text;
	int color_pair;
};

struct ColoredLine
{
	vector<ColoredSegment> segments;
};

// network

#undef FD_SETSIZE
#define FD_SETSIZE 512

// output

void LOG_Print( string message );
void CONSOLE_Print( string message, int color = dye_white, bool log = true, int tsline = 0);
void CONSOLE_PrintNoCRLF( string message, int color = dye_white, bool log = true, int tsline = 0);
void CONSOLE_ChangeChannel( string channel );
void CONSOLE_AddChannelUser( string name );
void CONSOLE_RemoveChannelUser( string name );
void CONSOLE_RemoveChannelUsers( );
void CONSOLE_Draw(int tsline = 0);
void CONSOLE_Resize( );
void Process_Command( );

//
// CGProxy
//

class CTCPServer;
class CTCPSocket;
class CTCPClient;
class CUDPSocket;
class CUDPServer;
class CBonjour;
class CBNET;
class CIncomingGameHost;
class CGameProtocol;
class CGPSProtocol;
class CCommandPacket;

class CGProxy
{
public:
	string m_Version;
	CTCPServer *m_LocalServer;
	CTCPSocket *m_LocalSocket;
	CTCPClient *m_RemoteSocket;
	CUDPSocket *m_UDPSocket;
	CUDPServer *m_UDPCommandSocket;
	CBNET *m_BNET;
	vector<CIncomingGameHost *> m_Games;
	CGameProtocol *m_GameProtocol;
	CGPSProtocol *m_GPSProtocol;
	queue<CCommandPacket *> m_LocalPackets;
	queue<CCommandPacket *> m_RemotePackets;
	queue<CCommandPacket *> m_PacketBuffer;
	vector<unsigned char> m_Laggers;
	uint32_t m_TotalPacketsReceivedFromLocal;
	uint32_t m_TotalPacketsReceivedFromRemote;
	bool m_Exiting;
	bool m_TFT;
	string m_War3Path;
	string m_CDKeyROC;
	string m_CDKeyTFT;
	string m_Server;
	string m_Username;
	string m_Password;
	string m_Channel;
	uint32_t m_War3Version;
	uint16_t m_Port;
	uint32_t m_LastConnectionAttemptTime;
	uint32_t m_LastRefreshTime;
	string m_RemoteServerIP;
	uint16_t m_RemoteServerPort;
	bool m_GameIsReliable;
	bool m_GameStarted;
	bool m_LeaveGameSent;
	bool m_ActionReceived;
	bool m_Synchronized;
	uint16_t m_ReconnectPort;
	unsigned char m_PID;
	unsigned char m_ChatPID;
	uint32_t m_ReconnectKey;
	unsigned char m_NumEmptyActions;
	unsigned char m_NumEmptyActionsUsed;
	uint32_t m_LastAckTime;
	uint32_t m_LastActionTime;
	string m_JoinedName;
	string m_HostName;
	string m_LastIp;
	bool m_inconsole;
	uint16_t m_GUIPort;
	uint16_t m_CMDPort;
	bool m_Console;
	bool m_UDPConsole;
	string m_UDPPassword;
	bool m_Quit;
	bool m_PublicGames;
	bool m_FilterGProxy;
	vector<string> m_Hosts;
	vector<string> m_UDPUsers;
	vector<string> m_IPUsers;

	CBonjour* m_Bonjour;

	CGProxy( string nHosts, string nUDPBindIP, uint16_t nUDPPort, uint16_t nGUIPort, bool nUDPConsole, bool nPublicGames, bool nFilterGProxy, string nUDPPassword, string nUDPTrigger, bool nTFT, string nWar3Path, string nCDKeyROC, string nCDKeyTFT, string nServer, string nUsername, string nPassword, string nChannel, uint32_t nWar3Version, uint16_t nPort, BYTEARRAY nEXEVersion, BYTEARRAY nEXEVersionHash, string nPasswordHashType );
	~CGProxy( );

	// processing functions

	bool Update( long usecBlock );

	void ExtractLocalPackets( );
	void ProcessLocalPackets( );
	void ExtractRemotePackets( );
	void ProcessRemotePackets( );

	bool AddGame( CIncomingGameHost *game );
	void SendLocalChat( string message );
	void SendEmptyAction( );

	void UDPChatDel(string ip);
	void UDPChatAdd(string ip);
	bool UDPChatAuth(string ip);
	void UDPChatSendBack(string s);
	void UDPChatSend(string s);
	void UDPChatSend(BYTEARRAY b);

	void ReloadConfig ();
	void UDPCommands( string Message );
};

extern uint32_t War3Version;
extern CConfig CFG;
extern CGProxy* gGProxy;
extern bool gRestart;
extern string gInputBuffer;


// ImGui thread management
extern std::thread* g_ImGuiThread;
extern CGProxy* gGProxy;

#endif
