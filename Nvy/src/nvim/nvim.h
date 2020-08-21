#pragma once

enum class NvimMessageType {
	Response = 1,
	Notification = 2
};
enum NvimMethod : uint8_t {
	vim_get_api_info = 0,
	nvim_input = 1,
	nvim_input_mouse = 2
};
constexpr const char *NVIM_METHOD_NAMES[] {
	"vim_get_api_info",
	"nvim_input",
	"nvim_input_mouse"
};
enum NvimOutboundNotification : uint8_t {
	nvim_ui_attach = 0
};
constexpr const char *NVIM_OUTBOUND_NOTIFICATION_NAMES[] {
	"nvim_ui_attach"
};
enum class MouseButton {
	Left,
	Right,
	Middle,
	Wheel
};
enum class MouseAction {
	Press,
	Drag,
	Release,
	MouseWheelUp,
	MouseWheelDown,
	MouseWheelLeft,
	MouseWheelRight
};

struct Nvim {
	uint64_t api_level;

	uint32_t current_msg_id;
	std::vector<NvimMethod> msg_id_to_method;

	HWND hwnd;
	HANDLE stdin_read;
	HANDLE stdin_write;
	HANDLE stdout_read;
	HANDLE stdout_write;
	PROCESS_INFORMATION process_info;
};

void NvimInitialize(Nvim *nvim, HWND hwnd);
void NvimShutdown(Nvim *nvim);

void NvimUIAttach(Nvim *nvim, uint32_t grid_width, uint32_t grid_height);

void NvimSendInput(Nvim *nvim, char input_char);
void NvimSendInput(Nvim *nvim, const char* input_chars);
void NvimSendInput(Nvim *nvim, int virtual_key);
void NvimSendMouseInput(Nvim *nvim, MouseButton button, MouseAction action, uint32_t mouse_row, uint32_t mouse_col);