#include "pch.h"
#include "nvim.h"

#include "mpack/mpack.h"

#include "nvim/mpack_helper.h"

void ProcessMPackResponse(HWND hwnd, NvimMethod method, mpack_node_t params) {
	switch (method) {
	case NvimMethod::vim_get_api_info: {
		mpack_node_t top_level_map = mpack_node_array_at(params, 1);
		mpack_node_t version_map = mpack_node_map_value_at(top_level_map, 0);
		uint64_t api_level = mpack_node_map_cstr(version_map, "api_level").data->value.u;
		PostMessage(hwnd, WM_NVIM_SET_API_LEVEL, api_level, 0);
	} break;
	}
}

void ProcessMPackNotification(HWND hwnd, NvimInboundNotification notification, mpack_node_t params) {
	switch (notification) {
	case NvimInboundNotification::redraw: {
		mpack_node_print_to_stdout(params);
	} break;
	}
}

void ProcessMPackResult(mpack_node_t root, Nvim *nvim) {
	NvimMessageType message_type = static_cast<NvimMessageType>(mpack_node_array_at(root, 0).data->value.i);

	if (message_type == NvimMessageType::Response) {
		uint64_t msg_id = mpack_node_array_at(root, 1).data->value.u;

		// TODO: handle error
		assert(mpack_node_array_at(root, 2).data->type == mpack_type_nil);

		std::lock_guard<std::mutex> guard(nvim->msgid_mutex);
		assert(nvim->msgid_to_method.size() >= msg_id);
		ProcessMPackResponse(nvim->hwnd, nvim->msgid_to_method[msg_id], mpack_node_array_at(root, 3));
	}
	else if (message_type == NvimMessageType::Notification) {
		assert(mpack_node_array_at(root, 1).data->type == mpack_type_str);
		const char *str = mpack_node_str(mpack_node_array_at(root, 1));
		int strlen = static_cast<int>(mpack_node_strlen(mpack_node_array_at(root, 1)));
		printf("Received notification %.*s\n", strlen, str);
		
		if (!strncmp(str, "redraw", strlen)) {
			ProcessMPackNotification(nvim->hwnd, NvimInboundNotification::redraw, mpack_node_array_at(root, 2));
		}
	}
}

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

	mpack_tree_t tree;
	mpack_tree_init_stream(&tree, ReadFromNvim, nvim->stdout_read, 1024 * 4096, 4096 * 4);

	while(true) {
		mpack_tree_parse(&tree);
		auto err = mpack_tree_error(&tree);
		if (mpack_tree_error(&tree) != mpack_ok) {
			break;
		}

		ProcessMPackResult(mpack_tree_root(&tree), nvim);
		//MPackSendNotification(nvim, NvimOutboundNotification::nvim_ui_attach, 200, 100, MPACK_MAP(1, "ext_linegrid", true));
	}

	printf("Nvim Message Handler Died\n");
	return 0;
}

//DWORD WINAPI NvimMessageHandler(LPVOID param) {
//	Nvim *nvim = reinterpret_cast<Nvim *>(param);
//	for (;;) {
//		void *buffer = malloc(1024 * 1024);
//
//		DWORD bytes_read;
//		BOOL b = ReadFile(nvim->stdout_read, buffer, 1024 * 1024, &bytes_read, nullptr);
//		assert(b);
//
//		printf("Bytes read: %i\n", bytes_read);
//
//		PostMessage(nvim->hwnd, WM_NVIM_MESSAGE, reinterpret_cast<WPARAM>(buffer), bytes_read);
//	}
//}

void NvimInitialize(HWND hwnd, Nvim *nvim) {
	nvim->hwnd = hwnd;
	nvim->api_level = 0;
	nvim->current_msgid = 0;

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

void NvimUIAttach(Nvim *nvim) {
	MPackSendNotification(nvim, NvimOutboundNotification::nvim_ui_attach, 200, 100, MPACK_MAP(1, "ext_linegrid", true));
}

void NvimProcessResult(NvimMethod method_name, mpack_node_t result, Nvim *nvim) {
	switch (method_name) {
	case NvimMethod::vim_get_api_info: {
		mpack_node_t top_level_map = mpack_node_array_at(result, 1);
		mpack_node_t version_map = mpack_node_map_value_at(top_level_map, 0);
		nvim->api_level = mpack_node_map_cstr(version_map, "api_level").data->value.u;
		MPackSendNotification(nvim, NvimOutboundNotification::nvim_ui_attach, 200, 100, MPACK_MAP(1, "ext_linegrid", true));

	} break;
	}
}

void NvimProcessNotification(NvimInboundNotification notification_name, mpack_node_t result, Nvim *nvim) {
	switch (notification_name) {
	case NvimInboundNotification::redraw: {
		mpack_node_print_to_stdout(result);
		__debugbreak();
	} break;
	}
}