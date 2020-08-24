#include "pch.h"
#include "common/mpack_helper.h"
#include "nvim/nvim.h"
#include "renderer/renderer.h"

struct Context {
	Nvim *nvim;
	Renderer *renderer;
	WINDOWPLACEMENT saved_window_placement;
	GridPoint cached_cursor_pos;
};

#include <chrono>
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
		}
	}
	else if (result.type == MPackMessageType::Notification) {
		if (MPackMatchString(result.notification.name, "redraw")) {
			auto t1 = std::chrono::high_resolution_clock::now();
			RendererRedraw(context->renderer, result.params);
			auto t2 = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
			printf("Dur: %llu microseconds\n", duration);
		}
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	Context *context = reinterpret_cast<Context *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (!context) {
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}
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
		else {
			NvimSendInput(context->nvim, static_cast<int>(wparam));
		}
	} return 0;
	case WM_MOUSEMOVE: {
		GridPoint cursor_pos = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		if (context->cached_cursor_pos.col != cursor_pos.col || context->cached_cursor_pos.row != cursor_pos.row) {
			switch (wparam) {
			case MK_LBUTTON: {
				NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Drag, cursor_pos.row, cursor_pos.col);
			} break;
			case MK_MBUTTON: {
				NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Drag, cursor_pos.row, cursor_pos.col);
			} break;
			case MK_RBUTTON: {
				NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Drag, cursor_pos.row, cursor_pos.col);
			} break;
			}
			context->cached_cursor_pos = cursor_pos;
		}
	} return 0;
	case WM_LBUTTONDOWN: {
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Press, row, col);
	} return 0;
	case WM_RBUTTONDOWN: {
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Press, row, col);
	} return 0;
	case WM_MBUTTONDOWN: {
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Press, row, col);
	} return 0;
	case WM_LBUTTONUP: {
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Release, row, col);
	} return 0;
	case WM_RBUTTONUP: {
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Release, row, col);
	} return 0;
	case WM_MBUTTONUP: {
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Release, row, col);
	} return 0;
	case WM_MOUSEWHEEL: {
		bool should_resize_font = (GetKeyState(VK_CONTROL) & 0x80) != 0;

		short wheel_distance = GET_WHEEL_DELTA_WPARAM(wparam);
		short scroll_amount = wheel_distance / WHEEL_DELTA;
		auto [row, col] = RendererCursorToGridPoint(context->renderer, MAKEPOINTS(lparam));
		MouseAction action = scroll_amount > 0 ? MouseAction::MouseWheelUp : MouseAction::MouseWheelDown;

		if (should_resize_font) {
			RendererUpdateFont(context->renderer, (context->renderer->font_size / context->renderer->dpi_scale) + (scroll_amount * 2.0f));
			auto [rows, cols] = RendererPixelsToGridSize(context->renderer,
				context->renderer->pixel_size.width, context->renderer->pixel_size.height);
			if (rows != context->renderer->grid_rows || cols != context->renderer->grid_cols) {
				NvimSendResize(context->nvim, rows, cols);
			}
		}
		// TODO: Scroll bug
		else {
			for (int i = 0; i < abs(scroll_amount); ++i) {
				NvimSendMouseInput(context->nvim, MouseButton::Wheel, action, row, col);
			}
		}
	} return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void OpenConsole() {
	FILE *dummy;
	AllocConsole();
	freopen_s(&dummy, "CONIN$", "r", stdin);
	freopen_s(&dummy, "CONOUT$", "w", stdout);
	freopen_s(&dummy, "CONOUT$", "w", stderr);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR p_cmd_line, int n_cmd_show) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
	OpenConsole();

	const wchar_t *window_class_name = L"Nvy_Class";
	const wchar_t *window_title = L"Nvy";

	WNDCLASSEX window_class {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = instance,
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = nullptr,
		.lpszClassName = window_class_name
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
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		nullptr,
		nullptr,
		instance,
		nullptr
	);
	if (hwnd == NULL) return 1;
	ShowWindow(hwnd, n_cmd_show);

	Renderer renderer {};
	RendererInitialize(&renderer, hwnd, "Fira Code", 25.0f);

	Nvim nvim {};
	NvimInitialize(&nvim, hwnd);

	Context context {
		.nvim = &nvim,
		.renderer = &renderer,
		.saved_window_placement = WINDOWPLACEMENT { .length = sizeof(WINDOWPLACEMENT) }
	};
	SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&context));

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