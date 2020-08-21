#include "pch.h"
#include "nvim.h"

#include "common/mpack_helper.h"

void MPackSendRequest(Nvim *nvim, NvimMethod method, std::function<void(mpack_writer_t *writer)> Callback = nullptr) {
	char *data;
	size_t size;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &data, &size);

	mpack_start_array(&writer, 4);
	mpack_write_i64(&writer, 0);
	mpack_write_i64(&writer, nvim->current_msg_id++);
	nvim->msg_id_to_method.push_back(method);
	mpack_write_cstr(&writer, NVIM_METHOD_NAMES[method]);

	// TODO: REDO
	if (Callback) {
		Callback(&writer);
	}
	else {
		mpack_start_array(&writer, 0);
		mpack_finish_array(&writer);
	}

	mpack_finish_array(&writer);

	// TODO: Error Handle
	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the data!\n");
		return;
	}

	DWORD bytes_written;
	BOOL b = WriteFile(nvim->stdin_write, data, static_cast<DWORD>(size), &bytes_written, nullptr);
	assert(b);

	free(data);
}

void MPackSendNotification(Nvim *nvim, NvimOutboundNotification notification, std::function<void(mpack_writer_t *writer)> Callback = nullptr) {
	char *data;
	size_t size;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &data, &size);

	mpack_start_array(&writer, 3);
	mpack_write_i64(&writer, 2);
	mpack_write_cstr(&writer, NVIM_OUTBOUND_NOTIFICATION_NAMES[notification]);

	// TODO: REDO
	if (Callback) {
		Callback(&writer);
	}
	else {
		mpack_start_array(&writer, 0);
		mpack_finish_array(&writer);
	}

	mpack_finish_array(&writer);

	// TODO: Error Handle
	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the data!\n");
		return;
	}

	DWORD bytes_written;
	BOOL b = WriteFile(nvim->stdin_write, data, static_cast<DWORD>(size), &bytes_written, nullptr);
	assert(b);

	free(data);
}

static uint64_t ReadFromNvim(mpack_tree_t *tree, char *buffer, size_t count) {
	HANDLE nvim_stdout_read = mpack_tree_context(tree);
	DWORD bytes_read;
	BOOL success = ReadFile(nvim_stdout_read, buffer, static_cast<DWORD>(count), &bytes_read, nullptr);
	if (!success) {
		mpack_tree_flag_error(tree, mpack_error_io);
	}
	printf("Bytes read %u\n", bytes_read);
	return static_cast<uint64_t>(bytes_read);
}

DWORD WINAPI NvimMessageHandler(LPVOID param) {
	Nvim *nvim = reinterpret_cast<Nvim *>(param);
	mpack_tree_t *tree = reinterpret_cast<mpack_tree_t *>(malloc(sizeof(mpack_tree_t)));
	mpack_tree_init_stream(tree, ReadFromNvim, nvim->stdout_read, 1024 * 4096, 4096 * 4);

	while(true) {
		mpack_tree_parse(tree);
		if (mpack_tree_error(tree) != mpack_ok) {
			break;
		}

		void *msg_data = malloc(tree->data_length);
		memcpy(msg_data, tree->data, tree->data_length);
		mpack_tree_t *msg_tree = static_cast<mpack_tree_t *>(malloc(sizeof(mpack_tree_t)));
		mpack_tree_init_data(msg_tree, static_cast<char *>(msg_data), tree->data_length);
		mpack_tree_parse(msg_tree);

		PostMessage(nvim->hwnd, WM_NVIM_MESSAGE, reinterpret_cast<WPARAM>(msg_tree), 0);
	}

	// TODO: Error handle
	mpack_tree_destroy(tree);
	free(tree);
	printf("Nvim Message Handler Died\n");
	return 0;
}

void NvimInitialize(Nvim *nvim, HWND hwnd) {
	nvim->hwnd = hwnd;
	
	// TODO: Error handle win32 api calls
	HANDLE job_object = CreateJobObject(nullptr, nullptr);
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
	AssignProcessToJobObject(job_object, nvim->process_info.hProcess);

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

void NvimSendInput(Nvim *nvim, char input_char) {
	MPackSendRequest(nvim, NvimMethod::nvim_input,
		[=](mpack_writer_t *writer) {
			mpack_start_array(writer, 1);
			mpack_write_str(writer, &input_char, 1);
			mpack_finish_array(writer);
		}
	);
}

void NvimSendInput(Nvim *nvim, const char *input_string) {
	MPackSendRequest(nvim, NvimMethod::nvim_input,
		[=](mpack_writer_t *writer) {
			mpack_start_array(writer, 1);
			mpack_write_cstr(writer, input_string);
			mpack_finish_array(writer);
		}
	);
}
