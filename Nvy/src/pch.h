#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>

#include "mpack/mpack.h"

// WPARAM: mpack_tree_t *, LPARAM: none
#define WM_NVIM_MESSAGE WM_USER