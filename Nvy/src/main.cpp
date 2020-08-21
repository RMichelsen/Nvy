#include "pch.h"
#include "common/mpack_helper.h"
#include "nvim/nvim.h"
#include "renderer/renderer.h"

struct Context {
	Nvim *nvim;
	Renderer *renderer;
};

void ProcessMPackMessage(Context *context, mpack_tree_t *tree) {
	mpack_node_t root = mpack_tree_root(tree);

	NvimMessageType message_type = static_cast<NvimMessageType>(mpack_node_array_at(root, 0).data->value.i);

	if (message_type == NvimMessageType::Response) {
		uint64_t msg_id = mpack_node_array_at(root, 1).data->value.u;

		// TODO: handle error
		assert(mpack_node_array_at(root, 2).data->type == mpack_type_nil);
		mpack_node_t result = mpack_node_array_at(root, 3);
		
		assert(context->nvim->msg_id_to_method.size() >= msg_id);
		switch (context->nvim->msg_id_to_method[msg_id]) {
		case NvimMethod::vim_get_api_info: {
			mpack_node_t top_level_map = mpack_node_array_at(result, 1);
			mpack_node_t version_map = mpack_node_map_value_at(top_level_map, 0);
			context->nvim->api_level = mpack_node_map_cstr(version_map, "api_level").data->value.u;

			int requested_rows = static_cast<uint32_t>(context->renderer->pixel_size.height / context->renderer->font_height);
			int requested_cols = static_cast<uint32_t>(context->renderer->pixel_size.width / context->renderer->font_width);
			printf("Requested INITAL %u:%u\n", requested_rows, requested_cols);
			NvimSendUIAttach(context->nvim, requested_rows, requested_cols);
		} break;
		}
	}
	else if (message_type == NvimMessageType::Notification) {
		mpack_node_t name = mpack_node_array_at(root, 1);
		mpack_node_t params = mpack_node_array_at(root, 2);
		const char *str = mpack_node_str(name);
		int strlen = static_cast<int>(mpack_node_strlen(name));

		if (MPackMatchString(name, "redraw")) {
			RendererRedraw(context->renderer, params);
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
			uint32_t requested_rows = static_cast<uint32_t>(new_height / context->renderer->font_height);
			uint32_t requested_cols = static_cast<uint32_t>(new_width / context->renderer->font_width);
			RendererResize(context->renderer, new_width, new_height);
			NvimSendResize(context->nvim, requested_rows, requested_cols);
		}
	} return 0;
	case WM_DESTROY: {
		PostQuitMessage(0);
	} return 0;
	case WM_NVIM_MESSAGE: {
		mpack_tree_t *tree = reinterpret_cast<mpack_tree_t *>(wparam);
		ProcessMPackMessage(context, tree);
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
	case WM_KEYDOWN: {
		NvimSendInput(context->nvim, static_cast<int>(wparam));
	} return 0;
	case WM_MOUSEMOVE: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
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
	} return 0;
	case WM_LBUTTONDOWN: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Press, cursor_pos.row, cursor_pos.col);
	} return 0;
	case WM_RBUTTONDOWN: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Press, cursor_pos.row, cursor_pos.col);
	} return 0;
	case WM_MBUTTONDOWN: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Press, cursor_pos.row, cursor_pos.col);
	} return 0;
	case WM_LBUTTONUP: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Release, cursor_pos.row, cursor_pos.col);
	} return 0;
	case WM_RBUTTONUP: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Release, cursor_pos.row, cursor_pos.col);
	} return 0;
	case WM_MBUTTONUP: {
		CursorPos cursor_pos = RendererTranslateMousePosToGrid(context->renderer, MAKEPOINTS(lparam));
		NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Release, cursor_pos.row, cursor_pos.col);
	} return 0;
	case WM_MOUSEWHEEL: {
		short wheel_delta = GET_WHEEL_DELTA_WPARAM(wparam);
		if (wheel_delta > 0) {
			NvimSendMouseInput(context->nvim, MouseButton::Wheel, MouseAction::MouseWheelUp, 0, 0);
		}
		else {
			NvimSendMouseInput(context->nvim, MouseButton::Wheel, MouseAction::MouseWheelDown, 0, 0);
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
	//WIN_CHECK(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

	const wchar_t *window_class_name = L"Nvy_Class";
	const wchar_t *window_title = L"Nvy";

	WNDCLASSEX window_class {
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = WndProc,
		.hInstance = instance,
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = nullptr, // TODO: Change to something else, if resize causes tearing
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
	RendererInitialize(&renderer, hwnd, L"Fira Code", 30.0f);

	Nvim nvim {};
	NvimInitialize(&nvim, hwnd);

	Context context {
		.nvim = &nvim,
		.renderer = &renderer
	};
	SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&context));

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	//CoUninitialize();
	RendererShutdown(&renderer);
	NvimShutdown(&nvim);
	UnregisterClass(window_class_name, instance);
	DestroyWindow(hwnd);
	return 0;
}