#include "pch.h"
#include "nvim/nvim.h"

struct Context {
	Nvim *nvim;
};

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
	case WM_NVIM_SET_API_LEVEL: {
		context->nvim->api_level = wparam;
		NvimUIAttach(context->nvim);
		//void *buffer = reinterpret_cast<void *>(wparam);
		//uint32_t size = static_cast<uint32_t>(lparam);
		//NvimProcessMessage(buffer, size, context->nvim);
		//free(buffer);
	} return 0;
	//case WM_CHAR:
	//	if (wparam >= 0x20 && wparam <= 0x7E && ImGui::GetCurrentContext()) {
	//		ImGui::GetIO().AddInputCharacter(static_cast<char>(wparam));
	//	}
	//	return 0;
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

	const wchar_t *window_class_name = L"Explora_Class";
	const wchar_t *window_title = L"Explora";

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

	//Renderer renderer {};
	//InitializeRenderer(&renderer, instance, hwnd);


	
	Nvim nvim;
	NvimInitialize(hwnd, &nvim);
	Context context {
		.nvim = &nvim
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