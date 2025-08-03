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

#include "gproxy.h"
#include "config.h"
#include "util.h"

#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <windows.h>
#include <codecvt>
#include <locale>
#include <fcntl.h>
#include <io.h>

//
// CConfig
//

CConfig :: CConfig( )
{

}

CConfig :: ~CConfig( )
{

}

FILE* OpenUtf8File(const string& utf8Path) 
{
	wstring wpath = Utf8ToWide(utf8Path);

	HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;

	int fd = _open_osfhandle((intptr_t)hFile, _O_RDONLY);
	if (fd == -1) 
	{
		CloseHandle(hFile);
		return nullptr;
	}

	FILE* fp = _fdopen(fd, "rb");
	if (!fp) 
	{
		CloseHandle(hFile);
		return nullptr;
	}

	return fp;
}

void CConfig::Read(string file)
{
	strFile = file;

	FILE* fp = OpenUtf8File(file);
	if (!fp)
	{
		CONSOLE_Print("[CONFIG] warning - unable to read file [" + file + "]");
		return;
	}

	CONSOLE_Print("[CONFIG] loading file [" + file + "]");

	char line[1024] = {};
	while (fgets(line, sizeof(line), fp))
	{
		std::string Line(line);
		// ignore blank lines and comments

		if (Line.empty() || Line[0] == '#')
			continue;

		// remove newlines and partial newlines to help fix issues with Windows formatted config files on Linux systems

		Line.erase(remove(Line.begin(), Line.end(), '\r'), Line.end());
		Line.erase(remove(Line.begin(), Line.end(), '\n'), Line.end());

		string::size_type Split = Line.find("=");

		if (Split == string::npos)
			continue;

		string::size_type KeyStart = Line.find_first_not_of(" ");
		string::size_type KeyEnd = Line.find(" ", KeyStart);
		string::size_type ValueStart = Line.find_first_not_of(" ", Split + 1);
		string::size_type ValueEnd = Line.size();

		if (ValueStart != string::npos)
			m_CFG[Line.substr(KeyStart, KeyEnd - KeyStart)] = Line.substr(ValueStart, ValueEnd - ValueStart);
	}

	fclose(fp);
}

bool CConfig :: Exists( string key )
{
	return m_CFG.find( key ) != m_CFG.end( );
}

int CConfig :: GetInt( string key, int x )
{
	if( m_CFG.find( key ) == m_CFG.end( ) )
		return x;
	else
		return atoi( m_CFG[key].c_str( ) );
}

string CConfig :: GetString( string key, string x )
{
	if( m_CFG.find( key ) == m_CFG.end( ) )
		return x;
	else
		return m_CFG[key];
}

void CConfig :: Set( string key, string x )
{
	m_CFG[key] = x;
}


void CConfig::ReplaceKeyValue(const std::string& key, const std::string& value)
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