// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// C RunTime Header Files
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#include <vector>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <locale>
#include <iostream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <memory>
#include <functional>
#include <fstream>

#include <Commdlg.h>
#include <OleIdl.h>
#include <Shlobj.h>

#include <atlbase.h>
#include <atlwin.h>

#undef min
#undef max

extern const wchar_t* g_szAppName;
