#pragma once

#ifdef WIN32
#include <stdint.h>
#endif

#define CURL_STATICLIB
#include <curl.h>

#pragma comment(lib, "libcurl.lib")
//#pragma comment(lib, "libssl.lib")
//#pragma comment(lib, "libcrypto.lib")

#define __STORMLIB_SELF__
#include <StormLib.h>
#pragma comment(lib, "StormLibRUS.lib")

#include "json.hpp"
using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

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
#include <d3d9.h>
#include <imgui.h>
#include <atomic>
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <commdlg.h>

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