#pragma once

enum class NvimMessageType {
	Response = 1,
	Notification = 2
};
enum NvimMethod : uint8_t {
	vim_get_api_info
};
constexpr const char *NVIM_METHOD_NAMES[] {
	"vim_get_api_info"
};
enum NvimOutboundNotification : uint8_t {
	nvim_ui_attach
};
constexpr const char *NVIM_OUTBOUND_NOTIFICATION_NAMES[] {
	"nvim_ui_attach"
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
