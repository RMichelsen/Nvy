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
			NvimUIAttach(context->nvim, context->renderer->grid_width, context->renderer->grid_height);
		} break;
		}
	}
	else if (message_type == NvimMessageType::Notification) {
		mpack_node_t name = mpack_node_array_at(root, 1);
		mpack_node_t params = mpack_node_array_at(root, 2);

		if (MPackMatchString(name, "redraw")) {
			RendererRedraw(context->renderer, params);
		}
	}

	mpack_tree_destroy(tree);
	free(tree);
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
	//case WM_SIZE:
	//	if (wparam != SIZE_MINIMIZED) {
	//		HandleResize(renderer, LOWORD(lparam), HIWORD(lparam));
	//	}
	//	return 0;
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
		std::string input_string = "<";

		if ((GetKeyState(VK_SHIFT) & 0x80) != 0) {
			input_string += "S-";
		}
		if ((GetKeyState(VK_CONTROL) & 0x80) != 0) {
			input_string += "C-";
		}
		if ((GetKeyState(VK_MENU) & 0x80) != 0) {
			input_string += "M-";
		}

		int virtual_key = static_cast<int>(wparam);
		switch (virtual_key) {
		case VK_BACK: {
			input_string += "BS";
		} break;
		case VK_TAB: {
			input_string += "TAB";
		} break;
		case VK_RETURN: {
			input_string += "CR";
		} break;
		case VK_ESCAPE: {
			input_string += "Esc";
		} break;
		case VK_PRIOR: {
			input_string += "PageUp";
		} break;
		case VK_NEXT: {
			input_string += "PageDown";
		} break;
		case VK_HOME: {
			input_string += "Home";
		} break;
		case VK_END: {
			input_string += "End";
		} break;
		case VK_INSERT: {
			input_string += "Insert";
		} break;
		case VK_DELETE: {
			input_string += "Del";
		} break;
		}

		input_string += ">";

		NvimSendInput(context->nvim, input_string.c_str());
	}
	//case WM_MOUSEMOVE:
	//	Input::mouse_pos[0] = static_cast<float>(GET_X_LPARAM(lparam));
	//	Input::mouse_pos[1] = static_cast<float>(GET_Y_LPARAM(lparam));
	//	return 0;
	//case WM_LBUTTONDOWN:
	//	Input::mouse_buttons[0] = true;
	//	return 0;
	//case WM_RBUTTONDOWN:
	//	Input::mouse_buttons[1] = true;
	//	return 0;
	//case WM_LBUTTONUP:
	//	Input::mouse_buttons[0] = false;
	//	return 0;
	//case WM_RBUTTONUP:
	//	Input::mouse_buttons[1] = false;
	//	return 0;
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
		2560,
		1440,
		nullptr,
		nullptr,
		instance,
		nullptr
	);
	if (hwnd == NULL) return 1;
	ShowWindow(hwnd, n_cmd_show);

	Renderer renderer {};
	RendererInitialize(&renderer, hwnd, L"Fira Code Retina", 20.0f);

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

	NvimShutdown(&nvim);
	UnregisterClass(window_class_name, instance);
	DestroyWindow(hwnd);
	return 0;
}