#include "nvim.h"
#include "common/mpack_helper.h"
#include "third_party/mpack/mpack.h"

constexpr int Megabytes(int n) {
    return 1024 * 1024 * n;
}

int64_t RegisterRequest(Nvim *nvim, NvimRequest request) {
	nvim->msg_id_to_method.push_back(request);
	return nvim->next_msg_id++;
}

static size_t ReadFromNvim(mpack_tree_t *tree, char *buffer, size_t count) {
	HANDLE nvim_stdout_read = mpack_tree_context(tree);
	DWORD bytes_read;
	BOOL success = ReadFile(nvim_stdout_read, buffer, static_cast<DWORD>(count), &bytes_read, nullptr);
	if (!success) {
		mpack_tree_flag_error(tree, mpack_error_io);
	}
	return bytes_read;
}

DWORD WINAPI NvimMessageHandler(LPVOID param) {
	Nvim *nvim = static_cast<Nvim *>(param);
	mpack_tree_t *tree = static_cast<mpack_tree_t *>(malloc(sizeof(mpack_tree_t)));
	mpack_tree_init_stream(tree, ReadFromNvim, nvim->stdout_read, Megabytes(20), 1'048'576);

	while (true) {
		mpack_tree_parse(tree);
		if (mpack_tree_error(tree) != mpack_ok) {
			break;
		}

		// Blocking, dubious thread safety. Seems to work though...
		SendMessage(nvim->hwnd, WM_NVIM_MESSAGE, reinterpret_cast<WPARAM>(tree), 0);
	}

	mpack_tree_destroy(tree);
	free(tree);
	PostMessage(nvim->hwnd, WM_DESTROY, 0, 0);
	return 0;
}

DWORD WINAPI NvimProcessMonitor(LPVOID param) {
	Nvim *nvim = static_cast<Nvim *>(param);
	while (true) {
		DWORD exit_code;
		if (GetExitCodeProcess(nvim->process_info.hProcess, &exit_code) && exit_code == STILL_ACTIVE) {
			Sleep(1);
		}
		else {
			nvim->exit_code = exit_code;
			break;
		}
	}
	PostMessage(nvim->hwnd, WM_DESTROY, 0, 0);
	return 0;
}

void NvimInitialize(Nvim *nvim, wchar_t *command_line, HWND hwnd) {
	nvim->hwnd = hwnd;

	HANDLE job_object = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info {
		.BasicLimitInformation = JOBOBJECT_BASIC_LIMIT_INFORMATION {
			.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
		}
	};
	SetInformationJobObject(job_object, JobObjectExtendedLimitInformation, &job_info, sizeof(job_info));
	
	SECURITY_ATTRIBUTES sec_attribs {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.bInheritHandle = true
	};
	HANDLE stdin_read, stdout_write, stderr_write;
	CreatePipe(&stdin_read, &nvim->stdin_write, &sec_attribs, 0);
	CreatePipe(&nvim->stdout_read, &stdout_write, &sec_attribs, 0);
	CreatePipe(&nvim->stderr_read, &stderr_write, &sec_attribs, 0);

	STARTUPINFO startup_info {
		.cb = sizeof(STARTUPINFO),
		.dwFlags = STARTF_USESTDHANDLES,
		.hStdInput = stdin_read,
		.hStdOutput = stdout_write,
		.hStdError = stderr_write
	};

	// wchar_t command_line[] = L"nvim --embed";
	CreateProcessW(
		nullptr,
		command_line,
		nullptr,
		nullptr,
		true,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startup_info,
		&nvim->process_info
	);
	AssignProcessToJobObject(job_object, nvim->process_info.hProcess);

	// Close unneeded handles
	CloseHandle(stdin_read);
	CloseHandle(stdout_write);
	CloseHandle(stderr_write);
	CloseHandle(nvim->process_info.hThread);

	// Start process monitor thread
	DWORD _;
	CreateThread( nullptr, 0, NvimProcessMonitor, nvim, 0, &_);

	// Do the initial messages with nvim in sync
	mpack_tree_t *tree_reader = static_cast<mpack_tree_t *>(malloc(sizeof(mpack_tree_t)));
	mpack_tree_init_stream(tree_reader, ReadFromNvim, nvim->stdout_read, Megabytes(20), 1'048'576);

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);

	// Query api info
	MPackStartRequest(RegisterRequest(nvim, vim_get_api_info), NVIM_REQUEST_NAMES[vim_get_api_info], &writer);
	mpack_start_array(&writer, 0);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	if (!MPackSendData(nvim->stdin_write, data, size)) {
		return;
	}
	mpack_tree_parse(tree_reader);
	if (mpack_tree_error(tree_reader)) {
		return;
	}
	MPackMessageResult result = MPackExtractMessageResult(tree_reader);
	if (result.type == MPackMessageType::Response){
		mpack_node_t top_level_map = mpack_node_array_at(result.params, 1);
		mpack_node_t version_map = mpack_node_map_value_at(top_level_map, 0);
		int64_t api_level = mpack_node_map_cstr(version_map, "api_level").data->value.i;
		assert(api_level > 6);
	}

	// Set g:nvy global variable
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartNotification(NVIM_OUTBOUND_NOTIFICATION_NAMES[nvim_set_var], &writer);
	mpack_start_array(&writer, 2);
	mpack_write_cstr(&writer, "nvy");
	mpack_write_int(&writer, 1);
	mpack_finish_array(&writer);
	size = MPackFinishMessage(&writer);
	if (!MPackSendData(nvim->stdin_write, data, size)) {
		return;
	}

	// Setup neovim to send a blocking request so we can finalize seting up before
	// buffer
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_command), NVIM_REQUEST_NAMES[nvim_command], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, "autocmd VimEnter * call rpcrequest(1, 'vimenter')");
	mpack_finish_array(&writer);
	size = MPackFinishMessage(&writer);
	if (!MPackSendData(nvim->stdin_write, data, size)) {
		return;
	}
	mpack_tree_parse(tree_reader);
	if (mpack_tree_error(tree_reader)) {
		return;
	}
	result = MPackExtractMessageResult(tree_reader); // get the result just in case...

	mpack_tree_destroy(tree_reader);
	free(tree_reader);

	CreateThread(nullptr, 0, NvimMessageHandler, nvim, 0, &_);
}

void NvimShutdown(Nvim *nvim) {
	DWORD exit_code;
	GetExitCodeProcess(nvim->process_info.hProcess, &exit_code);

	if(exit_code == STILL_ACTIVE) {
		CloseHandle(nvim->stdin_write);
		CloseHandle(nvim->stdout_read);
		CloseHandle(nvim->stderr_read);
		TerminateProcess(nvim->process_info.hProcess, 0);
		CloseHandle(nvim->process_info.hProcess);
		CloseHandle(nvim->process_info.hThread);
	}
}

void NvimSendUIAttach(Nvim *nvim, int grid_rows, int grid_cols) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;

	// Send UI attach notification
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartNotification(NVIM_OUTBOUND_NOTIFICATION_NAMES[nvim_ui_attach], &writer);
	mpack_start_array(&writer, 3);
	mpack_write_int(&writer, grid_cols);
	mpack_write_int(&writer, grid_rows);
	mpack_start_map(&writer, 1);
	mpack_write_cstr(&writer, "ext_linegrid");
	mpack_write_true(&writer);
	mpack_finish_map(&writer);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSendResize(Nvim *nvim, int grid_rows, int grid_cols) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);

	MPackStartNotification(NVIM_OUTBOUND_NOTIFICATION_NAMES[nvim_ui_try_resize], &writer);
	mpack_start_array(&writer, 2);
	mpack_write_int(&writer, grid_cols);
	mpack_write_int(&writer, grid_rows);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSendModifiedInput(Nvim *nvim, const char *input) {
	bool shift_down = (GetKeyState(VK_SHIFT) & 0x80) != 0;
	bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x80) != 0;
	bool alt_down = (GetKeyState(VK_MENU) & 0x80) != 0;

	constexpr int MAX_INPUT_STRING_SIZE = 64;
	char input_string[MAX_INPUT_STRING_SIZE];

	snprintf(input_string, MAX_INPUT_STRING_SIZE, "<%s%s%s%s>", ctrl_down ? "C-" : "", 
			shift_down ? "S-" : "", alt_down ? "M-" : "", input);

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_input), NVIM_REQUEST_NAMES[nvim_input], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, input_string);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSendChar(Nvim *nvim, wchar_t input_char) {
	// If the space is simply a regular space,
	// simply send the modified input
	if(input_char == VK_SPACE) {
		NvimSendModifiedInput(nvim, "Space");
		return;
	}

	char utf8_encoded[64]{};
	if(!WideCharToMultiByte(CP_UTF8, 0, &input_char, 1, 0, 0, NULL, NULL)) {
		return;
	}
	WideCharToMultiByte(CP_UTF8, 0, &input_char, 1, utf8_encoded, 64, NULL, NULL);

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_input), NVIM_REQUEST_NAMES[nvim_input], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, utf8_encoded);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSendSysChar(Nvim *nvim, wchar_t input_char) {
	char utf8_encoded[64]{};
	if(!WideCharToMultiByte(CP_UTF8, 0, &input_char, 1, 0, 0, NULL, NULL)) {
		return;
	}
	WideCharToMultiByte(CP_UTF8, 0, &input_char, 1, utf8_encoded, 64, NULL, NULL);

	NvimSendModifiedInput(nvim, utf8_encoded);
}

void NvimSendInput(Nvim *nvim, const char *input_chars) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);

	MPackStartRequest(RegisterRequest(nvim, nvim_input), NVIM_REQUEST_NAMES[nvim_input], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, input_chars);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSendMouseInput(Nvim *nvim, MouseButton button, MouseAction action, int mouse_row, int mouse_col) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_input_mouse), NVIM_REQUEST_NAMES[nvim_input_mouse], &writer);
	mpack_start_array(&writer, 6);

	switch (button) {
	case MouseButton::Left: {
		mpack_write_cstr(&writer, "left");
	} break;
	case MouseButton::Right: {
		mpack_write_cstr(&writer, "right");
	} break;
	case MouseButton::Middle: {
		mpack_write_cstr(&writer, "middle");
	} break;
	case MouseButton::Wheel: {
		mpack_write_cstr(&writer, "wheel");
	} break;
	}
	switch (action) {
	case MouseAction::Press: {
		mpack_write_cstr(&writer, "press");
	} break;
	case MouseAction::Drag: {
		mpack_write_cstr(&writer, "drag");
	} break;
	case MouseAction::Release: {
		mpack_write_cstr(&writer, "release");
	} break;
	case MouseAction::MouseWheelUp: {
		mpack_write_cstr(&writer, "up");
	} break;
	case MouseAction::MouseWheelDown: {
		mpack_write_cstr(&writer, "down");
	} break;
	case MouseAction::MouseWheelLeft: {
		mpack_write_cstr(&writer, "left");
	} break;
	case MouseAction::MouseWheelRight: {
		mpack_write_cstr(&writer, "right");
	} break;
	}

	bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x80) != 0;
	bool shift_down = (GetKeyState(VK_SHIFT) & 0x80) != 0;
	bool alt_down = (GetKeyState(VK_MENU) & 0x80) != 0;
	constexpr int MAX_INPUT_STRING_SIZE = 64;
	char input_string[MAX_INPUT_STRING_SIZE];
	snprintf(input_string, MAX_INPUT_STRING_SIZE, "%s%s%s", ctrl_down ? "C-" : "", shift_down ? "S-" : "", alt_down ? "M-" : "");
	mpack_write_cstr(&writer, input_string);

	mpack_write_i64(&writer, 0);
	mpack_write_i64(&writer, mouse_row);
	mpack_write_i64(&writer, mouse_col);
	mpack_finish_array(&writer);

	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

bool NvimProcessKeyDown(Nvim *nvim, int virtual_key) {
	const char *key;
	switch (virtual_key) {
	case VK_BACK: {
		key = "BS";
	} break;
	case VK_TAB: {
		key = "Tab";
	} break;
	case VK_RETURN: {
		key = "CR";
	} break;
	case VK_ESCAPE: {
		key = "Esc";
	} break;
	case VK_PRIOR: {
		key = "PageUp";
	} break;
	case VK_NEXT: {
		key = "PageDown";
	} break;
	case VK_HOME: {
		key = "Home";
	} break;
	case VK_END: {
		key = "End";
	} break;
	case VK_LEFT: {
		key = "Left";
	} break;
	case VK_UP: {
		key = "Up";
	} break;
	case VK_RIGHT: {
		key = "Right";
	} break;
	case VK_DOWN: {
		key = "Down";
	} break;
	case VK_INSERT: {
		key = "Insert";
	} break;
	case VK_DELETE: {
		key = "Del";
	} break;
	case VK_NUMPAD0: {
		key = "k0";
	} break;
	case VK_NUMPAD1: {
		key = "k1";
	} break;
	case VK_NUMPAD2: {
		key = "k2";
	} break;
	case VK_NUMPAD3: {
		key = "k3";
	} break;
	case VK_NUMPAD4: {
		key = "k4";
	} break;
	case VK_NUMPAD5: {
		key = "k5";
	} break;
	case VK_NUMPAD6: {
		key = "k6";
	} break;
	case VK_NUMPAD7: {
		key = "k7";
	} break;
	case VK_NUMPAD8: {
		key = "k8";
	} break;
	case VK_NUMPAD9: {
		key = "k9";
	} break;
	case VK_MULTIPLY: {
		key = "kMultiply";
	} break;
	case VK_ADD: {
		key = "kPlus";
	} break;
	case VK_SEPARATOR: {
		key = "kComma";
	} break;
	case VK_SUBTRACT: {
		key = "kMinus";
	} break;
	case VK_DECIMAL: {
		key = "kPoint";
	} break;
	case VK_DIVIDE: {
		key = "kDivide";
	} break;
	case VK_F1: {
		key = "F1";
	} break;
	case VK_F2: {
		key = "F2";
	} break;
	case VK_F3: {
		key = "F3";
	} break;
	case VK_F4: {
		key = "F4";
	} break;
	case VK_F5: {
		key = "F5";
	} break;
	case VK_F6: {
		key = "F6";
	} break;
	case VK_F7: {
		key = "F7";
	} break;
	case VK_F8: {
		key = "F8";
	} break;
	case VK_F9: {
		key = "F9";
	} break;
	case VK_F10: {
		key = "F10";
	} break;
	case VK_F11: {
		key = "F11";
	} break;
	case VK_F12: {
		key = "F12";
	} break;
	case VK_F13: {
		key = "F13";
	} break;
	case VK_F14: {
		key = "F14";
	} break;
	case VK_F15: {
		key = "F15";
	} break;
	case VK_F16: {
		key = "F16";
	} break;
	case VK_F17: {
		key = "F17";
	} break;
	case VK_F18: {
		key = "F18";
	} break;
	case VK_F19: {
		key = "F19";
	} break;
	case VK_F20: {
		key = "F20";
	} break;
	case VK_F21: {
		key = "F21";
	} break;
	case VK_F22: {
		key = "F22";
	} break;
	case VK_F23: {
		key = "F23";
	} break;
	case VK_F24: {
		key = "F24";
	} break;
	default: {
	} return false;
	}

	NvimSendModifiedInput(nvim, key);
	return true;
}

void NvimGetOptionValue(Nvim *nvim, const char *option) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_get_option_value), NVIM_REQUEST_NAMES[nvim_get_option_value], &writer);
	mpack_start_array(&writer, 2);
	mpack_write_cstr(&writer, option);
	mpack_start_map(&writer, 0);
	mpack_finish_map(&writer);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimParseOptionValueStr(Nvim *nvim, mpack_node_t value_node, Vec<char> *value_out) {
	char path[MAX_PATH];
	const char *value_path = mpack_node_str(value_node);
	size_t value_path_strlen = mpack_node_strlen(value_node);
	if (value_path && value_path_strlen)
	{
		value_out->resize(value_path_strlen);
		memcpy(value_out->data(), value_path, value_path_strlen);
	}
}

void NvimSendCommand(Nvim *nvim, const char *command) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_command), NVIM_REQUEST_NAMES[nvim_command], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, command);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSendResponse(Nvim *nvim, int64_t req_id) {
	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);

	mpack_start_array(&writer, 4);
	mpack_write_i64(&writer, 1);
	mpack_write_i64(&writer, req_id);
	mpack_write_nil(&writer);
	mpack_write_int(&writer, 0);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimOpenFile(Nvim *nvim, const wchar_t *file_name, bool open_new_buffer) {
	char utf8_encoded[MAX_PATH]{};
	WideCharToMultiByte(CP_UTF8, 0, file_name, -1, utf8_encoded, MAX_PATH, NULL, NULL);

	char file_command[MAX_PATH + 8] = {};
	if (open_new_buffer) {
		strcpy_s(file_command, MAX_PATH, "new ");
	}
	else {
		strcpy_s(file_command, MAX_PATH, "e ");
	}
	strcat_s(file_command, MAX_PATH, utf8_encoded);

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_command), NVIM_REQUEST_NAMES[nvim_command], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, file_command);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimSetFocus(Nvim *nvim) {
	const char *set_focus_command = "doautocmd <nomodeline> FocusGained";

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_command), NVIM_REQUEST_NAMES[nvim_command], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, set_focus_command);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}

void NvimKillFocus(Nvim *nvim) {
	const char *set_focus_command = "doautocmd <nomodeline> FocusLost";

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_command), NVIM_REQUEST_NAMES[nvim_command], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, set_focus_command);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}
void NvimQuit(Nvim *nvim)
{
	const char *quit_command = "confirm qa";

	char data[MAX_MPACK_OUTBOUND_MESSAGE_SIZE];
	mpack_writer_t writer;
	mpack_writer_init(&writer, data, MAX_MPACK_OUTBOUND_MESSAGE_SIZE);
	MPackStartRequest(RegisterRequest(nvim, nvim_command), NVIM_REQUEST_NAMES[nvim_command], &writer);
	mpack_start_array(&writer, 1);
	mpack_write_cstr(&writer, quit_command);
	mpack_finish_array(&writer);
	size_t size = MPackFinishMessage(&writer);
	MPackSendData(nvim->stdin_write, data, size);
}
