#pragma once

enum class NvimMessageType {
	Response = 1,
	Notification = 2
};

enum NvimMethod : uint8_t {
	vim_get_api_info = 0
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

enum class NvimInboundNotification : uint8_t {
	redraw
};

struct Nvim {
	uint64_t api_level;

	std::mutex msgid_mutex;
	uint32_t current_msgid;
	std::vector<NvimMethod> msgid_to_method;

	HWND hwnd;
	HANDLE stdin_read;
	HANDLE stdin_write;
	HANDLE stdout_read;
	HANDLE stdout_write;
	PROCESS_INFORMATION process_info;
};

void NvimInitialize(HWND hwnd, Nvim *nvim);
void NvimShutdown(Nvim *nvim);

void NvimUIAttach(Nvim *nvim);

void NvimProcessMessage(void *buffer, uint32_t size, Nvim *nvim);

