#include "gproxy.h"
#include "CMDNSScanner.h"
#include "util.h"

uint32_t ToVersionFlattened(const uint8_t version)
{
	return 10000u + 100u * (uint32_t)(1 - 1) + (uint32_t)(version);
}

CBonjour::CBonjour()
{
	DNSServiceErrorType err = DNSServiceCreateConnection(&client);
	if (err) 
	{
		CONSOLE_Print("[MDNS] Failed to initialize MDNS service: " + CBonjour::ErrorCodeToString(err));
		return;
	}
	CONSOLE_Print("[MDNS] MDNS service initialized");
}

void CBonjour::AddGame(string gameName, uint16_t port, const uint8_t gameVersion)
{
	m_GameIsExpansion = true;
	m_GameVersion = gameVersion;
	m_Service = client;

	string regType = GetRegisterType();
	DNSServiceErrorType err = DNSServiceRegister(&m_Service, kDNSServiceFlagsShareConnection, kDNSServiceInterfaceIndexAny, gameName.c_str(), regType.c_str(), "local", nullptr, htons(port), 0, nullptr, nullptr, nullptr);
	if (err) 
	{
		CONSOLE_Print("[MDNS] DNSServiceRegister ERR: " + CBonjour::ErrorCodeToString(err));
		//m_Service = nullptr;
		return;
	}
	else 
	{
		CONSOLE_Print("[MDNS] DNSServiceRegister OK for <" + gameName + "> [" + to_string(gameVersion) + "]" + "Port: " + to_string(port));
	}
}

CBonjour :: ~CBonjour()
{
	if (client) DNSServiceRefDeallocate(client);
}

void CBonjour::Broadcast_Info(bool TFT, unsigned char war3Version, BYTEARRAY mapGameType, BYTEARRAY mapFlags, BYTEARRAY mapWidth, BYTEARRAY mapHeight, string gameName, string hostName, uint32_t hostTime, string mapPath, BYTEARRAY mapCRC, uint32_t slotsTotal, uint32_t slotsOpen, uint16_t port, uint32_t hostCounter, uint32_t entryKey, BYTEARRAY mapSHA1)
{
	bool exists = false;
	DNSServiceRef service1 = NULL;
	DNSRecordRef record = NULL;
	int err = 0;

	for (auto i = games.begin(); i != games.end(); i++)
	{
		if (std::get<1>(*i) == port)
		{
			std::get<2>(*i) = GetTime();
			exists = true;
			service1 = std::get<0>(*i);
			record = std::get<3>(*i);
		}
		else
		{
			if (std::get<2>(*i) < (GetTime() - 6))
			{
				DNSServiceRefDeallocate(std::get<0>(*i));
				games.erase(i--);
			}
		}
	}

	if (!exists)
	{
		string regType;
		regType.reserve(24);
		regType.append("_blizzard._udp");
		regType.append(",_w3xp"); 
		regType.append(UTIL_ToHexString(ToVersionFlattened(war3Version)));

		service1 = client;
		err = DNSServiceRegister(&service1, 0, kDNSServiceInterfaceIndexAny, gameName.c_str(), regType.c_str(), "local", NULL, htons(port), 0, NULL, nullptr, NULL);
		if (err)
		{
			CONSOLE_Print("[MDNS] DNSServiceRegister 1 failed: " + UTIL_ToString(err) + "\n");
			return;
		}
		CONSOLE_Print("[MDNS] Successfully registered game [" + gameName + "] on port " + UTIL_ToString(port));
	}

	std::string players_num = UTIL_ToString(1);
	std::string players_max = UTIL_ToString(slotsTotal);
	std::string secret = UTIL_ToString(entryKey);
	std::string time = UTIL_ToString(hostTime);
	std::string statstringD = std::string((char*)mapFlags.data(), mapFlags.size()) + std::string("\x00", 1) +
		std::string((char*)mapWidth.data(), mapWidth.size()) +
		std::string((char*)mapHeight.data(), mapHeight.size()) +
		std::string((char*)mapCRC.data(), mapCRC.size()) + mapPath + std::string("\0", 1) +
		hostName + std::string("\0\0", 2) + std::string((char*)mapSHA1.data(), mapSHA1.size());

	std::string statstringE;
	uint16_t size = static_cast<uint16_t>(statstringD.size());
	unsigned char Mask = 1;

	for (unsigned int i = 0; i < size; ++i)
	{
		if ((statstringD[i] % 2) == 0)
			statstringE.push_back(statstringD[i] + 1);
		else
		{
			statstringE.push_back(statstringD[i]);
			Mask |= 1 << ((i % 7) + 1);
		}

		if (i % 7 == 6 || i == size - 1)
		{
			statstringE.insert(statstringE.end() - 1 - (i % 7), Mask);
			Mask = 1;
		}
	}

	std::string game_data_d;
	if (war3Version >= 32) // try this for games with short names on pre32?
		game_data_d = gameName + std::string("\0\0", 2) + statstringE + std::string("\0", 1) + (char)slotsTotal +
		std::string("\0\0\0", 3) + std::string("\x01\x20\x43\x00", 4) +
		std::string(1, port % 0x100) + std::string(1, port >> 8);
	else
		game_data_d = std::string("\x01\x00\x00\x00", 4) + statstringE + std::string("\0", 1) + (char)slotsTotal +
		std::string("\0\0\0", 3) + gameName + std::string("\0\0", 2) +
		std::string(1, port % 0x100) + std::string(1, port >> 8);


	std::string game_data = base64_encode((unsigned char*)game_data_d.c_str(), game_data_d.size());

	std::string w66;
	w66 = std::string("\x0A") + ((char)gameName.size()) + gameName + std::string("\x10\0", 2) +
		"\x1A" + (char)(15 + players_num.size()) + "\x0A\x0Bplayers_num\x12" + (char)players_num.size() + players_num +
		"\x1A" + (char)(9 + gameName.size()) + "\x0A\x05_name\x12" + (char)gameName.size() + gameName +
		"\x1A" + (char)(15 + players_max.size()) + "\x0A\x0Bplayers_max\x12" + (char)players_max.size() + players_max +
		"\x1A" + (char)(20 + time.size()) + "\x0A\x10game_create_time\x12" + (char)time.size() + time +
		"\x1A\x0A\x0A\x05_type\x12\x01\x31" + "\x1A\x0D\x0A\x08_subtype\x12\x01\x30" +
		"\x1A" + (char)(15 + secret.size()) + "\x0A\x0Bgame_secret\x12" + (char)secret.size() + secret +
		"\x1A" + (char)(14 + game_data.size()) + "\x01\x0A\x09game_data\x12" + (char)game_data.size() + "\x01" + game_data +
		"\x1A\x0C\x0A\x07game_id\x12\x01" + ((war3Version >= 32) ? "1" : "2") +
		"\x1A\x0B\x0A\x06_flags\x12\x01\x30";

	if (!exists)
	{
		err = DNSServiceAddRecord(service1, &record, 0, 66, (uint16_t)w66.size(), w66.c_str(), 0);
	}
	else
	{
		err = DNSServiceUpdateRecord(service1, record, kDNSServiceFlagsForce, (uint16_t)w66.size(), w66.c_str(), 0);
	}

	if (err) 
		CONSOLE_Print("[MDNS] DNSServiceAddRecord 1 failed: " + UTIL_ToString(err) + "\n");
	else 
		games.emplace_back(service1, port, GetTime(), record);

	return;
}

string CBonjour::GetRegisterType()
{
	string regType;
	regType.reserve(24);
	regType.append("_blizzard._udp"); // 14 chars
	if (m_GameIsExpansion) {
		regType.append(",_w3xp"); // 6 chars
	}
	else {
		regType.append(",_war3");
	}
	regType.append(UTIL_ToHexString(ToVersionFlattened(m_GameVersion))); // 4 chars
	return regType;
}

string CBonjour::ErrorCodeToString(DNSServiceErrorType errCode)
{
	switch (errCode) {
	case kDNSServiceErr_NoError:
		return "ok";
	case kDNSServiceErr_Unknown:
		return "unknown";
	case kDNSServiceErr_NoSuchName:
		return "no such name";
	case kDNSServiceErr_NoMemory:
		return "no memory";
	case kDNSServiceErr_BadParam:
		return "bad param";
	case kDNSServiceErr_BadReference:
		return "bad reference";
	case kDNSServiceErr_BadState:
		return "bad state";
	case kDNSServiceErr_BadFlags:
		return "bad flags";
	case kDNSServiceErr_Unsupported:
		return "unsupported";
	case kDNSServiceErr_NotInitialized:
		return "not initialized";
	case kDNSServiceErr_AlreadyRegistered:
		return "already registered";
	case kDNSServiceErr_NameConflict:
		return "name conflict";
	case kDNSServiceErr_Invalid:
		return "invalid";
	case kDNSServiceErr_Firewall:
		return "firewall";
	case kDNSServiceErr_Incompatible:
		return "incompatible";
	case kDNSServiceErr_BadInterfaceIndex:
		return "bad interface index";
	case kDNSServiceErr_Refused:
		return "refused";
	case kDNSServiceErr_NoSuchRecord:
		return "no such record";
	case kDNSServiceErr_NoAuth:
		return "no auth";
	case kDNSServiceErr_NoSuchKey:
		return "no such key";
	case kDNSServiceErr_NATTraversal:
		return "NAT traversal";
	case kDNSServiceErr_DoubleNAT:
		return "double NAT";
	case kDNSServiceErr_BadTime:
		return "bad time";
	case kDNSServiceErr_BadSig:
		return "bad signature";
	case kDNSServiceErr_BadKey:
		return "bad key";
	case kDNSServiceErr_Transient:
		return "transient";
	case kDNSServiceErr_ServiceNotRunning:
		return "service not running";
	case kDNSServiceErr_NATPortMappingUnsupported:
		return "NAT port mapping unsupported";
	case kDNSServiceErr_NATPortMappingDisabled:
		return "NAT port mapping disabled";
	case kDNSServiceErr_NoRouter:
		return "no router";
	case kDNSServiceErr_PollingMode:
		return "polling mode";
	case kDNSServiceErr_Timeout:
		return "timeout";
	default:
		return "unknown " + to_string(errCode);
	}
}