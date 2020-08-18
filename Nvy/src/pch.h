#pragma once

#include <mutex>
#include <vector>

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <windows.h>

// WPARAM: api_level, LPARAM: none
#define WM_NVIM_SET_API_LEVEL WM_USER