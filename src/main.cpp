#include "nvim/nvim.h"
#include "renderer/renderer.h"

struct Context {
	Nvim *nvim;
	Renderer *renderer;
	WINDOWPLACEMENT saved_window_placement;
	GridPoint cached_cursor_grid_pos;
	bool xbuttons[2];
};

void ProcessMPackMessage(Context *context, mpack_tree_t *tree) {
	MPackMessageResult result = MPackExtractMessageResult(tree);

	if (result.type == MPackMessageType::Response) {
		assert(result.response.msg_id <= context->nvim->next_msg_id);
		switch (context->nvim->msg_id_to_method[result.response.msg_id]) {
		case NvimRequest::vim_get_api_info: {
			mpack_node_t top_level_map = mpack_node_array_at(result.params, 1);
			mpack_node_t version_map = mpack_node_map_value_at(top_level_map, 0);
			int64_t api_level = mpack_node_map_cstr(version_map, "api_level").data->value.i;
			assert(api_level > 6);
			auto [rows, cols] = RendererPixelsToGridSize(context->renderer, 
				context->renderer->pixel_size.width, context->renderer->pixel_size.height);
			NvimSendUIAttach(context->nvim, rows, cols);
		} break;
        case NvimRequest::nvim_input:
        case NvimRequest::nvim_input_mouse: {
        } break;
		}
	}
	else if (result.type == MPackMessageType::Notification) {
		if (MPackMatchString(result.notification.name, "redraw")) {
			RendererRedraw(context->renderer, result.params);
		}
	}
}

void ToggleFullscreen(HWND hwnd, Context *context) {
	DWORD style = GetWindowLong(hwnd, GWL_STYLE);
	MONITORINFO mi { .cbSize = sizeof(MONITORINFO) };
	if (style & WS_OVERLAPPEDWINDOW) {
		if (GetWindowPlacement(hwnd, &context->saved_window_placement) &&
			GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
			SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
			SetWindowPos(hwnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left,
				mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
	}
	else {
		SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(hwnd, &context->saved_window_placement);
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Context *context = reinterpret_cast<Context *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	// if (!context) {
	// 	return DefWindowProc(hwnd, msg, wparam, lparam);
	// }
	if (msg == WM_CREATE) {
		LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
		return 0;
	}

	switch (msg) {
	case WM_SIZE: {
		if (wparam != SIZE_MINIMIZED) {
			uint32_t new_width = LOWORD(lparam);
			uint32_t new_height = HIWORD(lparam);
			auto [rows, cols] = RendererPixelsToGridSize(context->renderer, new_width, new_height);
			RendererResize(context->renderer, new_width, new_height);
			NvimSendResize(context->nvim, rows, cols);
		}
	} return 0;
	case WM_DESTROY: {
		PostQuitMessage(0);
	} return 0;
	case WM_NVIM_MESSAGE: {
		mpack_tree_t *tree = reinterpret_cast<mpack_tree_t *>(wparam);
		ProcessMPackMessage(context, tree);
	} return 0;
	case WM_RENDERER_FONT_UPDATE: {
		auto [rows, cols] = RendererPixelsToGridSize(context->renderer,
			context->renderer->pixel_size.width, context->renderer->pixel_size.height);
		NvimSendResize(context->nvim, rows, cols);
	} return 0;
	case WM_CHAR: {
		// Special case for <LT>
		if (wparam == 0x3C) {
			NvimSendInput(context->nvim, "<LT>");
		}
		else if (wparam >= 0x20 && wparam <= 0x7E) {
			NvimSendInput(context->nvim, static_cast<char>(wparam));
		}
	} return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		// Special case for <ALT+ENTER> (fullscreen transition)
		if (((GetKeyState(VK_MENU) & 0x80) != 0) && wparam == VK_RETURN) {
			ToggleFullscreen(hwnd, context);
		}
		else {
			NvimSendInput(context->nvim, static_cast<int>(wparam));
		}
	} return 0;
	case WM_MOUSEMOVE: {
		POINTS cursor_pos = MAKEPOINTS(lparam);
		GridPoint grid_pos = RendererCursorToGridPoint(context->renderer, cursor_pos.x, cursor_pos.y);
		if (context->cached_cursor_grid_pos.col != grid_pos.col || context->cached_cursor_grid_pos.row != grid_pos.row) {
			switch (wparam) {
			case MK_LBUTTON: {
				NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Drag, grid_pos.row, grid_pos.col);
			} break;
			case MK_MBUTTON: {
				NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Drag, grid_pos.row, grid_pos.col);
			} break;
			case MK_RBUTTON: {
				NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Drag, grid_pos.row, grid_pos.col);
			} break;
			}
			context->cached_cursor_grid_pos = grid_pos;
		}
	} return 0;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP: {
		POINTS cursor_pos = MAKEPOINTS(lparam);
		auto [row, col] = RendererCursorToGridPoint(context->renderer, cursor_pos.x, cursor_pos.y);
		if (msg == WM_LBUTTONDOWN) {
			NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Press, row, col);
		}
		else if (msg == WM_MBUTTONDOWN) {
			NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Press, row, col);
		}
		else if (msg == WM_RBUTTONDOWN) {
			NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Press, row, col);
		}
		else if (msg == WM_LBUTTONUP) {
			NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Release, row, col);
		}
		else if (msg == WM_MBUTTONUP) {
			NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Release, row, col);
		}
		else if (msg == WM_RBUTTONUP) {
			NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Release, row, col);
		}
	} return 0;
	case WM_XBUTTONDOWN: {
		int button = GET_XBUTTON_WPARAM(wparam);
		if(button == XBUTTON1 && !context->xbuttons[0]) {
			NvimSendInput(context->nvim, "<C-o>");
			context->xbuttons[0] = true;
		}
		else if(button == XBUTTON2 && !context->xbuttons[1]) {
			NvimSendInput(context->nvim, "<C-i>");
			context->xbuttons[1] = true;
		}
	} return 0;
	case WM_XBUTTONUP: {
		int button = GET_XBUTTON_WPARAM(wparam);
		if(button == XBUTTON1) {
			context->xbuttons[0] = false;
		}
		else if(button == XBUTTON2) {
			context->xbuttons[1] = false;
		}
	} return 0;
	case WM_MOUSEWHEEL: {
		bool should_resize_font = (GetKeyState(VK_CONTROL) & 0x80) != 0;

		POINTS screen_point = MAKEPOINTS(lparam);
		POINT client_point {
			.x = static_cast<LONG>(screen_point.x),
			.y = static_cast<LONG>(screen_point.y),
		};
		ScreenToClient(hwnd, &client_point);

		short wheel_distance = GET_WHEEL_DELTA_WPARAM(wparam);
		short scroll_amount = wheel_distance / WHEEL_DELTA;
		auto [row, col] = RendererCursorToGridPoint(context->renderer, client_point.x, client_point.y);
		MouseAction action = scroll_amount > 0 ? MouseAction::MouseWheelUp : MouseAction::MouseWheelDown;

		if (should_resize_font) {
			RendererUpdateFont(context->renderer, context->renderer->last_requested_font_size + (scroll_amount * 2.0f));
			auto [rows, cols] = RendererPixelsToGridSize(context->renderer,
				context->renderer->pixel_size.width, context->renderer->pixel_size.height);
			if (rows != context->renderer->grid_rows || cols != context->renderer->grid_cols) {
				NvimSendResize(context->nvim, rows, cols);
			}
		}
		else {
			for (int i = 0; i < abs(scroll_amount); ++i) {
				NvimSendMouseInput(context->nvim, MouseButton::Wheel, action, row, col);
			}
		}
	} return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR p_cmd_line, int n_cmd_show) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

	int n_args;
	LPWSTR *cmd_line_args = CommandLineToArgvW(p_cmd_line, &n_args);
	bool start_maximized = false;
	int64_t rows = 0;
	int64_t cols = 0;

	for(int i = 0; i < n_args; ++i) {
		if(!wcscmp(cmd_line_args[i], L"--maximize")) {
			start_maximized = true;
		}
		else if(!wcsncmp(cmd_line_args[i], L"--geometry=", 11)) {
			wchar_t *end_ptr;
			cols = wcstol(&cmd_line_args[i][11], &end_ptr, 10);
			rows = wcstol(end_ptr + 1, nullptr, 10);
		}
	}

	Renderer renderer {};
	RendererInitialize(&renderer, "Consolas", 18.0f);
	Nvim nvim {};
	NvimInitialize(&nvim);
	Context context {
		.nvim = &nvim,
		.renderer = &renderer,
		.saved_window_placement = WINDOWPLACEMENT { .length = sizeof(WINDOWPLACEMENT) }
	};

	int initial_width = CW_USEDEFAULT;
	int initial_height = CW_USEDEFAULT;
	if(rows != 0 && cols != 0) {
		D2D1_SIZE_U initial_pixel_size = RendererGridToPixelSize(&renderer, rows, cols);
		initial_width = static_cast<int>(initial_pixel_size.width);
		initial_height = static_cast<int>(initial_pixel_size.height);
		// Adjust size to include title bar
		RECT adjusted_rect = {0, 0, initial_width, initial_height};
		AdjustWindowRect(&adjusted_rect, WS_OVERLAPPEDWINDOW, false);
		initial_width = adjusted_rect.right - adjusted_rect.left;
		initial_height = adjusted_rect.bottom - adjusted_rect.top;
	}

	const wchar_t *window_class_name = L"Nvy_Class";
	const wchar_t *window_title = L"Nvy";

	WNDCLASSEX window_class {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = instance,
        .hIcon = static_cast<HICON>(LoadImage(GetModuleHandle(NULL), L"NVIM_ICON", IMAGE_ICON, LR_DEFAULTSIZE, LR_DEFAULTSIZE, 0)),
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = nullptr,
		.lpszClassName = window_class_name,
        .hIconSm = static_cast<HICON>(LoadImage(GetModuleHandle(NULL), L"NVIM_ICON", IMAGE_ICON, LR_DEFAULTSIZE, LR_DEFAULTSIZE, 0))
	};

	if (!RegisterClassEx(&window_class)) {
		return 1;
	}

	HWND hwnd = CreateWindow(
		window_class_name,
		window_title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		initial_width,
		initial_height,
		nullptr,
		nullptr,
		instance,
		&context
	);
	if (hwnd == NULL) return 1;
	RendererAttach(&renderer, hwnd);
	NvimAttach(&nvim, hwnd);
	
	if(start_maximized) {
		ToggleFullscreen(hwnd, &context);
	}
	ShowWindow(hwnd, n_cmd_show);

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	RendererShutdown(&renderer);
	NvimShutdown(&nvim);
	UnregisterClass(window_class_name, instance);
	DestroyWindow(hwnd);

	return 0;
}
