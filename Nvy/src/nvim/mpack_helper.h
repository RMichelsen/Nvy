#pragma once
#include <type_traits>
#include <windows.h>
#include "mpack/mpack.h"

#include "nvim/nvim.h"

struct MPackArrayStart {
	uint32_t count;
};
struct MPackArrayEnd {};
struct MPackMapStart {
	uint32_t count;
};
struct MPackMapEnd {};

#define MPACK_ARRAY(COUNT, ...) MPackArrayStart { COUNT }, __VA_ARGS__, MPackArrayEnd {}
#define MPACK_MAP(COUNT, ...) MPackMapStart { COUNT }, __VA_ARGS__, MPackMapEnd {}

template<typename T>
constexpr int ArgDelta(T t) {
	if constexpr (std::is_same_v<T, MPackArrayStart>) {
		return -static_cast<int>(t.count);
	}
	else if constexpr (std::is_same_v<T, MPackMapStart>) {
		return -static_cast<int>(t.count * 2);
	}
	return 1;
}

template<typename... Args>
void MPackSendRequest(Nvim *nvim, NvimMethod method, Args... args) {
	std::lock_guard<std::mutex> guard(nvim->msgid_mutex);

	char *data;
	size_t size;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &data, &size);

	mpack_start_array(&writer, 4);
	mpack_write_i64(&writer, 0);
	mpack_write_i64(&writer, nvim->current_msgid++);
	nvim->msgid_to_method.push_back(method);
	mpack_write_cstr(&writer, NVIM_METHOD_NAMES[method]);

	int num_args = 0;
	if constexpr (sizeof...(Args) > 0) {
		num_args = (ArgDelta(args) + ...);
	}

	mpack_start_array(&writer, num_args);
	(MPackWrite(&writer, args), ...);
	mpack_finish_array(&writer);
	mpack_finish_array(&writer);

	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the data!\n");
		return;
	}

	DWORD bytes_written;
	BOOL b = WriteFile(nvim->stdin_write, data, static_cast<DWORD>(size), &bytes_written, nullptr);
	assert(b);

	free(data);
}

template<typename... Args>
void MPackSendNotification(Nvim *nvim, NvimOutboundNotification notification, Args... args) {
	std::lock_guard<std::mutex> guard(nvim->msgid_mutex);

	char *data;
	size_t size;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &data, &size);

	mpack_start_array(&writer, 3);
	mpack_write_i64(&writer, 2);
	mpack_write_cstr(&writer, NVIM_OUTBOUND_NOTIFICATION_NAMES[notification]);

	int num_args = 0;
	if constexpr (sizeof...(Args) > 0) {
		num_args = (ArgDelta(args) + ...);
	}

	mpack_start_array(&writer, num_args);
	(MPackWrite(&writer, args), ...);
	mpack_finish_array(&writer);
	mpack_finish_array(&writer);

	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the data!\n");
		return;
	}

	DWORD bytes_written;
	BOOL b = WriteFile(nvim->stdin_write, data, static_cast<DWORD>(size), &bytes_written, nullptr);
	assert(b);

	free(data);
}

template<typename T>
void MPackWrite(mpack_writer_t *writer, T val) {
	if constexpr (std::is_same_v<T, MPackArrayStart>) {
		mpack_start_array(writer, val.count);
	}
	if constexpr (std::is_same_v<T, MPackArrayEnd>) {
		mpack_finish_array(writer);
	}
	if constexpr (std::is_same_v<T, MPackMapStart>) {
		mpack_start_map(writer, val.count);
	}
	if constexpr (std::is_same_v<T, MPackMapEnd>) {
		mpack_finish_map(writer);
	}
	if constexpr (std::is_same_v<T, int>) {
		mpack_write_i64(writer, val);
	}
	if constexpr (std::is_same_v<T, void *>) {
		mpack_write_nil(writer);
	}
	if constexpr (std::is_same_v<T, bool>) {
		mpack_write_bool(writer, val);
	}
	if constexpr (std::is_same_v<T, double>) {
		mpack_write_double(writer, val);
	}
	if constexpr (std::is_same_v<T, const char *>) {
		mpack_write_cstr(writer, val);
	}
}