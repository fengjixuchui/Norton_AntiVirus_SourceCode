//
// IBM AntiVirus Immune System
//
// File Name: StrUtil.h
// Author:    Milosz Muszynski
//
// Generic String Utility Module
//
// $Log: $
//

#ifndef _STRUTIL_H_
#define _STRUTIL_H_


// credit to Mike Wilson


class StrUtil
{
public:
	// trimming the whitespace
	static std::string& trim		( std::string& str );
	static std::string& ltrim		( std::string& str );
	static std::string& rtrim		( std::string& str );
  
	// altering the case
	static std::string& strupr		( std::string& str );
	static std::string& strlwr		( std::string& str );

	// classifying
	static bool isWhitespace		( const std::string& str );
	static bool isInteger			( const std::string& str ); 

	// conversion
	static int  asInteger			( const std::string& str );
	static long asLong				( const std::string& str );

	// miscellaneous
	static void makeSureEndsWith	( std::string& s1, char c );
	static void makeSureStartsWith	( std::string& s1, const std::string& s2 );
	static bool contains			( const std::string& s, const std::string& s1 );
	static bool containsAnyCase		( const std::string& s, const std::string& s1 );
	static void delAll				( std::string& s1, const std::string& s2 );
	static void replaceAll			( std::string& s,  const std::string& s1, const std::string& s2 );
	static bool	startsWith			( const std::string& str, const std::string& str2 );
};


#endif _STRUTIL_H_
