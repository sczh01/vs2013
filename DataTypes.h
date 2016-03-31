#ifndef _DATA_TYPES_H
#define _DATA_TYPES_H

#if WIN32
	#include "windows.h"
#else
#define TRUE 1
#define FALSE 0

	typedef signed char INT8;
	typedef unsigned char UINT8;
	typedef signed short INT16;
	typedef unsigned short UINT16;
	typedef unsigned int UINT32;
	typedef int INT32;
	typedef char CHAR;
	typedef short SHORT;
	typedef long LONG;
	typedef int INT;
	typedef unsigned int UINT;
	typedef unsigned long DWORD;
	typedef unsigned char BYTE;
	typedef unsigned short WORD;
	typedef float FLOAT;
	typedef double DOUBLE;
	typedef unsigned int BOOL;
#endif

#endif
