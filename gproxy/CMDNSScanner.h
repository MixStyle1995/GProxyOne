#pragma once


#include <vector>
#include <string>
#include <atomic>
#include <iostream>
#include <thread>
#include <mutex>
#include <tuple>
#include <string>
#include <codecvt>
#include <stdexcept>
#include <algorithm>
#include <iterator>

#include "util.h"

#include <dns_sd.h>

using namespace std;

class CBonjour
{
public:
	DNSServiceRef               m_Service;
	bool                        m_GameIsExpansion;
	uint8_t						m_GameVersion;
	DNSServiceRef				client;
	vector<tuple<DNSServiceRef, uint16_t, uint32_t, DNSRecordRef>> games;

	CBonjour::CBonjour();
	~CBonjour();
	
	void AddGame(string gameName, uint16_t port, const uint8_t gameVersion);
	void Broadcast_Info(bool TFT, unsigned char war3Version, BYTEARRAY mapGameType, BYTEARRAY mapFlags, BYTEARRAY mapWidth, BYTEARRAY mapHeight, string gameName, string hostName, uint32_t hostTime, string mapPath, BYTEARRAY mapCRC, uint32_t slotsTotal, uint32_t slotsOpen, uint16_t port, uint32_t hostCounter, uint32_t entryKey, BYTEARRAY mapSHA1);

	std::string GetRegisterType();
	std::string static ErrorCodeToString(DNSServiceErrorType errCode);
};