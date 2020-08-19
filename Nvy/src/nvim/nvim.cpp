#include "pch.h"
#include "nvim.h"

#include "nvim/mpack_helper.h"

static uint64_t ReadFromNvim(mpack_tree_t *tree, char *buffer, size_t count) {
	HANDLE nvim_stdout_read = mpack_tree_context(tree);
	DWORD bytes_read;
	BOOL success = ReadFile(nvim_stdout_read, buffer, static_cast<DWORD>(count), &bytes_read, nullptr);
	if (!success) {
		mpack_tree_flag_error(tree, mpack_error_io);
	}
	return static_cast<uint64_t>(bytes_read);
}

DWORD WINAPI NvimMessageHandler(LPVOID param) {
	Nvim *nvim = reinterpret_cast<Nvim *>(param);

	while(true) {
		mpack_tree_t *tree = reinterpret_cast<mpack_tree_t *>(malloc(sizeof(mpack_tree_t)));
		mpack_tree_init_stream(tree, ReadFromNvim, nvim->stdout_read, 1024 * 4096, 4096 * 4);

		mpack_tree_parse(tree);
		auto err = mpack_tree_error(tree);
		if (mpack_tree_error(tree) != mpack_ok) {
			break;
		}

		PostMessage(nvim->hwnd, WM_NVIM_MESSAGE, reinterpret_cast<WPARAM>(tree), 0);
	}

	printf("Nvim Message Handler Died\n");
	return 0;
}

void NvimInitialize(Nvim *nvim, HWND hwnd) {
	nvim->hwnd = hwnd;

	SECURITY_ATTRIBUTES sec_attribs {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.bInheritHandle = true
	};
	CreatePipe(&nvim->stdin_read, &nvim->stdin_write, &sec_attribs, 0);
	CreatePipe(&nvim->stdout_read, &nvim->stdout_write, &sec_attribs, 0);

	STARTUPINFO startup_info {
		.cb = sizeof(STARTUPINFO),
		.dwFlags = STARTF_USESTDHANDLES,
		.hStdInput = nvim->stdin_read,
		.hStdOutput = nvim->stdout_write,
		.hStdError = nvim->stdout_write
	};

	wchar_t command_line[] = L"nvim --embed";
	CreateProcess(
		nullptr,
		command_line,
		nullptr,
		nullptr,
		true,
		0,
		nullptr,
		nullptr,
		&startup_info,
		&nvim->process_info
	);

	DWORD thread_id;
	HANDLE thread = CreateThread(
		nullptr,
		0,
		NvimMessageHandler,
		nvim,
		0,
		&thread_id
	);

	MPackSendRequest(nvim, NvimMethod::vim_get_api_info);
}

void NvimShutdown(Nvim *nvim) {
	CloseHandle(nvim->stdin_read);
	CloseHandle(nvim->stdin_write);
	CloseHandle(nvim->stdout_read);
	CloseHandle(nvim->stdout_write);
	CloseHandle(nvim->process_info.hThread);
	TerminateProcess(nvim->process_info.hProcess, 0);
	CloseHandle(nvim->process_info.hProcess);
}

void NvimUIAttach(Nvim *nvim, uint32_t grid_width, uint32_t grid_height) {
	MPackSendNotification(nvim, NvimOutboundNotification::nvim_ui_attach,
		[=](mpack_writer_t *writer) {
			mpack_start_array(writer, 3);
			mpack_write_int(writer, grid_width);
			mpack_write_int(writer, grid_height);
			mpack_start_map(writer, 1);
			mpack_write_cstr(writer, "ext_linegrid");
			mpack_write_true(writer);
			mpack_finish_map(writer);
			mpack_finish_array(writer);
		}
	);
}