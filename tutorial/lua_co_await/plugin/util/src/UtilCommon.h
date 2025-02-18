// UtilCommon.h
// PaintDream (paintdream@paintdream.com)
// 2024-07-31
//

#pragma once

#include "../../../src/common.h"

#if !IRIS_MONOLITHIC
#ifdef UTIL_EXPORT
	#ifdef __GNUC__
		#define UTIL_API __attribute__ ((visibility ("default")))
	#else
		#define UTIL_API __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
	#endif
#else
	#ifdef __GNUC__
		#define UTIL_API __attribute__ ((visibility ("default")))
	#else
		#define UTIL_API __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
	#endif
#endif
#else
#define UTIL_API
#endif
