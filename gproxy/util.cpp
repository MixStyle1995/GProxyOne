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
#include "util.h"

#include <sys/stat.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <windows.h>
#include <codecvt>
#include <locale>
#include <fcntl.h>
#include <io.h>

BYTEARRAY UTIL_CreateByteArray( unsigned char *a, int size )
{
	if( size < 1 )
		return BYTEARRAY( );

	return BYTEARRAY( a, a + size );
}

BYTEARRAY UTIL_CreateByteArray( char *a, int size )
{
	BYTEARRAY result;

	for( int i = 0; i < size; i++ )
		result.push_back( a[i] );

	return result;
}

BYTEARRAY UTIL_CreateByteArray( unsigned char c )
{
	BYTEARRAY result;
	result.push_back( c );
	return result;
}

BYTEARRAY UTIL_CreateByteArray( uint16_t i, bool reverse )
{
	BYTEARRAY result;
	result.push_back( (unsigned char)i );
	result.push_back( (unsigned char)( i >> 8 ) );

	if( reverse )
		return BYTEARRAY( result.rbegin( ), result.rend( ) );
	else
		return result;
}

BYTEARRAY UTIL_CreateByteArray( uint32_t i, bool reverse )
{
	BYTEARRAY result;
	result.push_back( (unsigned char)i );
	result.push_back( (unsigned char)( i >> 8 ) );
	result.push_back( (unsigned char)( i >> 16 ) );
	result.push_back( (unsigned char)( i >> 24 ) );

	if( reverse )
		return BYTEARRAY( result.rbegin( ), result.rend( ) );
	else
		return result;
}

uint16_t UTIL_ByteArrayToUInt16( BYTEARRAY b, bool reverse, unsigned int start )
{
	if( b.size( ) < start + 2 )
		return 0;

	BYTEARRAY temp = BYTEARRAY( b.begin( ) + start, b.begin( ) + start + 2 );

	if( reverse )
		temp = BYTEARRAY( temp.rbegin( ), temp.rend( ) );

	return (uint16_t)( temp[1] << 8 | temp[0] );
}

uint32_t UTIL_ByteArrayToUInt32( BYTEARRAY b, bool reverse, unsigned int start )
{
	if( b.size( ) < start + 4 )
		return 0;

	BYTEARRAY temp = BYTEARRAY( b.begin( ) + start, b.begin( ) + start + 4 );

	if( reverse )
		temp = BYTEARRAY( temp.rbegin( ), temp.rend( ) );

	return (uint32_t)( temp[3] << 24 | temp[2] << 16 | temp[1] << 8 | temp[0] );
}

string UTIL_ByteArrayToDecString( BYTEARRAY b )
{
	if( b.empty( ) )
		return string( );

	string result = UTIL_ToString( b[0] );

	for( BYTEARRAY :: iterator i = b.begin( ) + 1; i != b.end( ); i++ )
		result += " " + UTIL_ToString( *i );

	return result;
}

string UTIL_ByteArrayToHexString( BYTEARRAY b )
{
	if( b.empty( ) )
		return string( );

	string result = UTIL_ToHexString( b[0] );

	for( BYTEARRAY :: iterator i = b.begin( ) + 1; i != b.end( ); i++ )
	{
		if( *i < 16 )
			result += " 0" + UTIL_ToHexString( *i );
		else
			result += " " + UTIL_ToHexString( *i );
	}

	return result;
}

void UTIL_AppendByteArray( BYTEARRAY &b, BYTEARRAY append )
{
	b.insert( b.end( ), append.begin( ), append.end( ) );
}

void UTIL_AppendByteArrayFast( BYTEARRAY &b, BYTEARRAY &append )
{
	b.insert( b.end( ), append.begin( ), append.end( ) );
}

void UTIL_AppendByteArray( BYTEARRAY &b, unsigned char *a, int size )
{
	UTIL_AppendByteArray( b, UTIL_CreateByteArray( a, size ) );
}

void UTIL_AppendByteArray( BYTEARRAY &b, string append, bool terminator )
{
	// append the string plus a null terminator

	b.insert( b.end( ), append.begin( ), append.end( ) );

	if( terminator )
		b.push_back( 0 );
}

void UTIL_AppendByteArrayFast( BYTEARRAY &b, string &append, bool terminator )
{
	// append the string plus a null terminator

	b.insert( b.end( ), append.begin( ), append.end( ) );

	if( terminator )
		b.push_back( 0 );
}

void UTIL_AppendByteArray( BYTEARRAY &b, uint16_t i, bool reverse )
{
	UTIL_AppendByteArray( b, UTIL_CreateByteArray( i, reverse ) );
}

void UTIL_AppendByteArray( BYTEARRAY &b, uint32_t i, bool reverse )
{
	UTIL_AppendByteArray( b, UTIL_CreateByteArray( i, reverse ) );
}

BYTEARRAY UTIL_ExtractCString( BYTEARRAY &b, unsigned int start )
{
	// start searching the byte array at position 'start' for the first null value
	// if found, return the subarray from 'start' to the null value but not including the null value

	if( start < b.size( ) )
	{
		for( unsigned int i = start; i < b.size( ); i++ )
		{
			if( b[i] == 0 )
				return BYTEARRAY( b.begin( ) + start, b.begin( ) + i );
		}

		// no null value found, return the rest of the byte array

		return BYTEARRAY( b.begin( ) + start, b.end( ) );
	}

	return BYTEARRAY( );
}

unsigned char UTIL_ExtractHex( BYTEARRAY &b, unsigned int start, bool reverse )
{
	// consider the byte array to contain a 2 character ASCII encoded hex value at b[start] and b[start + 1] e.g. "FF"
	// extract it as a single decoded byte

	if( start + 1 < b.size( ) )
	{
		unsigned int c;
		string temp = string( b.begin( ) + start, b.begin( ) + start + 2 );

		if( reverse )
			temp = string( temp.rend( ), temp.rbegin( ) );

		stringstream SS;
		SS << temp;
		SS >> hex >> c;
		return c;
	}

	return 0;
}

BYTEARRAY UTIL_ExtractNumbers( string s, unsigned int count )
{
	// consider the string to contain a bytearray in dec-text form, e.g. "52 99 128 1"

	BYTEARRAY result;
	unsigned int c;
	stringstream SS;
	SS << s;

	for( unsigned int i = 0; i < count; i++ )
	{
		if( SS.eof( ) )
			break;

		SS >> c;

		// todotodo: if c > 255 handle the error instead of truncating

		result.push_back( (unsigned char)c );
	}

	return result;
}

BYTEARRAY UTIL_ExtractHexNumbers( string s )
{
	// consider the string to contain a bytearray in hex-text form, e.g. "4e 17 b7 e6"

	BYTEARRAY result;
	unsigned int c;
	stringstream SS;
	SS << s;

	while( !SS.eof( ) )
	{
		SS >> hex >> c;

		// todotodo: if c > 255 handle the error instead of truncating

		result.push_back( (unsigned char)c );
	}

	return result;
}

string UTIL_ToString( unsigned long i )
{
	string result;
	stringstream SS;
	SS << i;
	SS >> result;
	return result;
}

string UTIL_ToString( unsigned short i )
{
	string result;
	stringstream SS;
	SS << i;
	SS >> result;
	return result;
}

string UTIL_ToString( unsigned int i )
{
	string result;
	stringstream SS;
	SS << i;
	SS >> result;
	return result;
}

string UTIL_ToString( long i )
{
	string result;
	stringstream SS;
	SS << i;
	SS >> result;
	return result;
}

string UTIL_ToString( short i )
{
	string result;
	stringstream SS;
	SS << i;
	SS >> result;
	return result;
}

string UTIL_ToString( int i )
{
	string result;
	stringstream SS;
	SS << i;
	SS >> result;
	return result;
}

string UTIL_ToString( float f, int digits )
{
	string result;
	stringstream SS;
	SS << std :: fixed << std :: setprecision( digits ) << f;
	SS >> result;
	return result;
}

string UTIL_ToString( double d, int digits )
{
	string result;
	stringstream SS;
	SS << std :: fixed << std :: setprecision( digits ) << d;
	SS >> result;
	return result;
}

string UTIL_ToHexString( uint32_t i )
{
	string result;
	stringstream SS;
	SS << std :: hex << i;
	SS >> result;
	return result;
}

// todotodo: these UTIL_ToXXX functions don't fail gracefully, they just return garbage (in the uint case usually just -1 casted to an unsigned type it looks like)

uint16_t UTIL_ToUInt16( string &s )
{
	uint16_t result;
	stringstream SS;
	SS << s;
	SS >> result;
	return result;
}

uint32_t UTIL_ToUInt32( string &s )
{
	uint32_t result;
	stringstream SS;
	SS << s;
	SS >> result;
	return result;
}

int16_t UTIL_ToInt16( string &s )
{
	int16_t result;
	stringstream SS;
	SS << s;
	SS >> result;
	return result;
}

int32_t UTIL_ToInt32( string &s )
{
	int32_t result;
	stringstream SS;
	SS << s;
	SS >> result;
	return result;
}

double UTIL_ToDouble( string &s )
{
	double result;
	stringstream SS;
	SS << s;
	SS >> result;
	return result;
}

string UTIL_MSToString( uint32_t ms )
{
	string MinString = UTIL_ToString( ( ms / 1000 ) / 60 );
	string SecString = UTIL_ToString( ( ms / 1000 ) % 60 );

	if( MinString.size( ) == 1 )
		MinString.insert( 0, "0" );

	if( SecString.size( ) == 1 )
		SecString.insert( 0, "0" );

	return MinString + "m" + SecString + "s";
}

bool UTIL_FileExists( string file )
{
	struct stat fileinfo;

	if( stat( file.c_str( ), &fileinfo ) == 0 )
		return true;

	return false;
}

string UTIL_FileRead( string file, uint32_t start, uint32_t length )
{
	ifstream IS(file.c_str(), ios::binary);
	if( !IS)
	{
		CONSOLE_Print( "[UTIL] warning - unable to read file part [" + file + "]" );
		return string( );
	}

	// get length of file

	IS.seekg( 0, ios :: end );
	std::streamoff FileLengthStream = IS.tellg();

	if (FileLengthStream == -1) 
	{
		IS.close();
		return string();
	}

	uint32_t FileLength = static_cast<uint32_t>(FileLengthStream);
	if( start > FileLength )
	{
		IS.close( );
		return string( );
	}

	if (start + length > FileLength)
		length = FileLength - start;

	// Read data
	IS.seekg(start, ios::beg);
	vector<char> Buffer(length);
	IS.read(Buffer.data(), length);

	size_t bytesRead = static_cast<size_t>(IS.gcount());
	string BufferString(Buffer.data(), bytesRead);
	IS.close();

	return BufferString;
}

string UTIL_FileRead( string file )
{
	ifstream IS(file.c_str(), ios::binary);

	if(!IS)
	{
		CONSOLE_Print( "[UTIL] warning - unable to read file [" + file + "]" );
		return string( );
	}

	// get length of file

	IS.seekg( 0, ios :: end );
	uint32_t FileLength = static_cast<uint32_t>(IS.tellg( ));
	IS.seekg( 0, ios :: beg );

	// read data

	char *Buffer = new char[FileLength];
	IS.read( Buffer, FileLength );
	size_t bytesRead = static_cast<size_t>(IS.gcount());
	string BufferString = string( Buffer, bytesRead);
	IS.close( );
	delete [] Buffer;

	if( BufferString.size( ) == FileLength )
		return BufferString;
	else
		return string( );
}

bool UTIL_FileWrite( string file, unsigned char *data, uint32_t length )
{
	ofstream OS;
	OS.open( file.c_str( ), ios :: binary );

	if( OS.fail( ) )
	{
		CONSOLE_Print( "[UTIL] warning - unable to write file [" + file + "]" );
		return false;
	}

	// write data

	OS.write( (const char *)data, length );
	OS.close( );
	return true;
}

string UTIL_FileSafeName( string fileName )
{
	string :: size_type BadStart = fileName.find_first_of( "\\/:*?<>|" );

	while( BadStart != string :: npos )
	{
		fileName.replace( BadStart, 1, 1, '_' );
		BadStart = fileName.find_first_of( "\\/:*?<>|" );
	}

	return fileName;
}

string UTIL_AddPathSeperator( string path )
{
	if( path.empty( ) )
		return string( );

#ifdef WIN32
	char Seperator = '\\';
#else
	char Seperator = '/';
#endif

	if( *(path.end( ) - 1) == Seperator )
		return path;
	else
		return path + string( 1, Seperator );
}

BYTEARRAY UTIL_EncodeStatString( BYTEARRAY &data )
{
	unsigned char Mask = 1;
	BYTEARRAY Result;

	for( unsigned int i = 0; i < data.size( ); i++ )
	{
		if( ( data[i] % 2 ) == 0 )
			Result.push_back( data[i] + 1 );
		else
		{
			Result.push_back( data[i] );
			Mask |= 1 << ( ( i % 7 ) + 1 );
		}

		if( i % 7 == 6 || i == data.size( ) - 1 )
		{
			Result.insert( Result.end( ) - 1 - ( i % 7 ), Mask );
			Mask = 1;
		}
	}

	return Result;
}

BYTEARRAY UTIL_DecodeStatString( BYTEARRAY &data )
{
	unsigned char Mask;
	BYTEARRAY Result;

	for( unsigned int i = 0; i < data.size( ); i++ )
	{
		if( ( i % 8 ) == 0 )
			Mask = data[i];
		else
		{
			if( ( Mask & ( 1 << ( i % 8 ) ) ) == 0 )
				Result.push_back( data[i] - 1 );
			else
				Result.push_back( data[i] );
		}
	}

	return Result;
}

void UTIL_AddStrings( vector<string> &dest, vector<string> sourc )
{
	bool n;
	for(uint32_t o=0; o<sourc.size(); o++)
	{
		n = true;
		for(uint32_t p=0; p<dest.size(); p++)
		{
			if (sourc[o]==dest[p])
				n = false;
		}
		if (n)
			dest.push_back(sourc[o]);
	}
}

void UTIL_ExtractStrings( string s, vector<string> &v )
{
	// consider the string to contain strings, e.g. "one two three"
	v.clear();
	string istr;
	stringstream SS;
	SS << s;
	while( SS >> istr ) v.push_back(istr);

	return;
}

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

inline bool is_base64(unsigned char c) {
	return (isalnum(c) || (c == '+') || (c == '/'));
}

std::string base64_encode(const unsigned char* bytes_to_encode, size_t in_len) {
	std::string ret;
	int i = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	while (in_len--) {
		char_array_3[i++] = *(bytes_to_encode++);
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; i < 4; i++)
				ret += base64_chars[char_array_4[i]];

			i = 0;
		}
	}

	if (i > 0) {
		for (int j = i; j < 3; j++)
			char_array_3[j] = 0;

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (int j = 0; j < i + 1; j++)
			ret += base64_chars[char_array_4[j]];

		while (i++ < 3)
			ret += '=';
	}

	return ret;
}

void AppendByteArrayString(vector<uint8_t>& b, const std::string& append, bool terminator)
{
	// append the string plus a null terminator

	b.insert(end(b), begin(append), end(append));

	if (terminator)
		b.push_back(0);
}

void AppendProtoBufferFromLengthDelimitedS2S(vector<uint8_t>& b, const string& key, const string& value)
{
	size_t thisSize = key.size() + value.size() + 4;
	b.reserve(b.size() + thisSize + 2);
	b.push_back(0x1A);
	b.push_back((uint8_t)thisSize);
	b.push_back(0x0A);
	b.push_back((uint8_t)key.size());
	AppendByteArrayString(b, key, false);
	b.push_back(0x12);
	b.push_back((uint8_t)value.size());
	AppendByteArrayString(b, value, false);
}

void AppendProtoBufferFromLengthDelimitedS2C(vector<uint8_t>& b, const string& key, const uint8_t value)
{
	size_t thisSize = key.size() + 5;
	b.reserve(b.size() + thisSize + 2);
	b.push_back(0x1A);
	b.push_back((uint8_t)thisSize);
	b.push_back(0x0A);
	b.push_back((uint8_t)key.size());
	AppendByteArrayString(b, key, false);
	b.push_back(0x12);
	b.push_back(1);
	b.push_back(value);
}

void AppendByteArrayFast(vector<uint8_t>& b, const vector<uint8_t>& append)
{
	b.insert(end(b), begin(append), end(append));
}

void AppendByteArray(vector<uint8_t>& b, const uint32_t i, bool bigEndian)
{
	size_t offset = b.size();
	b.resize(offset + 4);
	uint8_t* cursor = b.data() + offset;
	if (bigEndian) {
		cursor[0] = static_cast<uint8_t>(i >> 24);
		cursor[1] = static_cast<uint8_t>(i >> 16);
		cursor[2] = static_cast<uint8_t>(i >> 8);
		cursor[3] = static_cast<uint8_t>(i);
	}
	else {
		cursor[0] = static_cast<uint8_t>(i);
		cursor[1] = static_cast<uint8_t>(i >> 8);
		cursor[2] = static_cast<uint8_t>(i >> 16);
		cursor[3] = static_cast<uint8_t>(i >> 24);
	}
}

vector<uint8_t> EncodeStatString(const string& data)
{
	uint8_t b = 0, mask = 1;
	size_t i = 0, j = 0, w = 1;
	size_t len = data.size();
	size_t fullBlockCount = len / 7u;
	size_t fullBlockLen = fullBlockCount * 7u;
	size_t outSize = fullBlockCount * 8u;
	if (fullBlockLen < len) {
		outSize += (len % 7u) + 1u;
	}
	vector<uint8_t> result;
	result.resize(outSize);

	for (; i < fullBlockLen; i += 7, w++) {
		mask = 1;
		for (j = 0; j < 7; j++, w++) {
			b = data[i + j];
			if (b % 2 == 0) {
				result[w] = b + 1;
			}
			else {
				result[w] = b;
				mask |= (1u << (j + 1u));
			}
		}
		result[w - 8] = mask;
	}

	if (fullBlockLen < len) {
		mask = 1;
		for (i = fullBlockLen; i < len; i++, w++) {
			b = data[i];
			if (b % 2 == 0) {
				result[w] = b + 1;
			}
			else {
				result[w] = b;
				mask |= (1u << ((i % 7u) + 1u));
			}
		}
		result[fullBlockCount * 8] = mask;
	}


	return result;
}

wstring Utf8ToWide(const string& str)
{
	if (str.empty()) return wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	wstring wstr(size_needed - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
	return wstr;
}