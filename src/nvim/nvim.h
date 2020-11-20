#pragma once

enum NvimRequest : uint8_t {
	vim_get_api_info = 0,
	nvim_input = 1,
	nvim_input_mouse = 2,
	nvim_eval = 3,
	nvim_command = 4
};
constexpr const char *NVIM_REQUEST_NAMES[] {
	"nvim_get_api_info",
	"nvim_input",
	"nvim_input_mouse",
	"nvim_eval",
	"nvim_command"
};
enum NvimOutboundNotification : uint8_t {
	nvim_ui_attach = 0,
	nvim_ui_try_resize = 1,
	nvim_set_var = 2
};
constexpr const char *NVIM_OUTBOUND_NOTIFICATION_NAMES[] {
	"nvim_ui_attach",
	"nvim_ui_try_resize",
	"nvim_set_var"
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
constexpr int MAX_MPACK_OUTBOUND_MESSAGE_SIZE = 4096;

struct Nvim {
	int64_t next_msg_id;
	Vec<NvimRequest> msg_id_to_method;

	HWND hwnd;
	HANDLE stdin_read;
	HANDLE stdin_write;
	HANDLE stdout_read;
	HANDLE stdout_write;
	PROCESS_INFORMATION process_info;
};

void NvimInitialize(Nvim *nvim, wchar_t *command_line, HWND hwnd);
void NvimShutdown(Nvim *nvim);

void NvimParseConfig(Nvim *nvim, mpack_node_t config_node, Vec<char> *guifont_out);

void NvimSendUIAttach(Nvim *nvim, int grid_rows, int grid_cols);
void NvimSendResize(Nvim *nvim, int grid_rows, int grid_cols);
void NvimSendChar(Nvim *nvim, wchar_t input_char);
void NvimSendSysChar(Nvim *nvim, wchar_t sys_char);
void NvimSendInput(Nvim *nvim, const char* input_chars);
void NvimSendInput(Nvim *nvim, int virtual_key, int flags);
void NvimSendMouseInput(Nvim *nvim, MouseButton button, MouseAction action, int mouse_row, int mouse_col);
bool NvimProcessKeyDown(Nvim *nvim, int virtual_key);
void NvimOpenFile(Nvim *nvim, const char *file_name);
