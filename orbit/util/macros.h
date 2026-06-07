// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_UTIL_MACROS_H_
#define ORBIT_UTIL_MACROS_H_

// from <bits/wordsize.h>
#ifndef __WORDSIZE
#if (defined __x86_64__ && !defined __ILP32__) || defined __LP64__
# define __WORDSIZE	64
#else
# define __WORDSIZE    32
#endif
#endif

// WORD SIZE
#if defined _WIN64 || __WORDSIZE == 64
#define _ORBIT_ENVIRON_64BIT_
#define _ORBIT_ENVIRON 64
#elif defined(_WIN32) || __WORDSIZE == 32
#define _ORBIT_ENVIRON_32BIT
#define _ORBIT_ENVIRON 32
#endif

// OS PLATFORM
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define _ORBIT_PLATFORM_WINDOWS
#define _ORBIT_PLATFORM_NAME "windows"
#define _ORBIT_PLATFORM_PATHSEP "\\"
#define _ORBIT_PLATFORM_PATHSSEP ";"
#ifdef _ORBITAPI_LIB
#define _ORBITAPI __declspec(dllimport)
#else
#define _ORBITAPI __declspec(dllexport)
#endif
#elif defined(__APPLE__)
#define _ORBIT_PLATFORM_DARWIN
#define _ORBIT_PLATFORM_NAME "darwin"
#define _ORBIT_PLATFORM_PATHSEP "/"
#define _ORBIT_PLATFORM_PATHSSEP ":"
#define _ORBITAPI
#elif defined(__linux__)
#define _ORBIT_PLATFORM_LINUX
#define _ORBIT_PLATFORM_NAME "linux"
#define _ORBIT_PLATFORM_PATHSEP "/"
#define _ORBIT_PLATFORM_PATHSSEP ":"
#define _ORBITAPI
#elif defined(__unix__)
#define _ORBIT_PLATFORM_UNIX
#define _ORBIT_PLATFORM_NAME "unix"
#define _ORBIT_PLATFORM_PATHSEP "/"
#define _ORBIT_PLATFORM_PATHSSEP ":"
#define _ORBITAPI
#else
#define _ORBIT_PLATFORM_NAME "unknown"
#define _ORBITAPI
#endif

#endif // !ORBIT_UTIL_MACROS_H_
