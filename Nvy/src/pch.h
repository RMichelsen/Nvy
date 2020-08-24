#pragma once

#include <string>

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <windows.h>

#include <d3d11_4.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <dwrite_3.h>

#include "mpack/mpack.h"

#include "common/vec.h"

// WPARAM: mpack_tree_t *, LPARAM: none
#define WM_NVIM_MESSAGE WM_USER

// WPARAM: none, LPARAM: none
#define WM_RENDERER_FONT_UPDATE (WM_USER + 1)
