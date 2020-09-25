#pragma once
#include <cassert>
#include "third_party/mpack/mpack.h"

inline int MPackIntFromArray(mpack_node_t arr, int index) {
	return static_cast<int>(mpack_node_array_at(arr, index).data->value.i);
}

inline bool MPackMatchString(mpack_node_t node, const char *str_to_match) {
	assert(node.data->type == mpack_type_str);
	const char *str = mpack_node_str(node);
	return strncmp(str, str_to_match, mpack_node_strlen(node)) == 0;
}

enum class MPackMessageType {
	Request = 0,
	Response = 1,
	Notification = 2
};
struct MPackRequest {
	mpack_node_t method;
	int64_t msg_id;
};
struct MPackResponse {
	mpack_node_t error;
	int64_t msg_id;
};
struct MPackNotification {
	mpack_node_t name;
};
struct MPackMessageResult {
	MPackMessageType type;
	mpack_node_t params;
	union {
		MPackRequest request;
		MPackResponse response;
		MPackNotification notification;
	};
};

inline void MPackStartRequest(int64_t msg_id, const char *request, mpack_writer_t *writer) {
	mpack_start_array(writer, 4);
	mpack_write_i64(writer, 0);
	mpack_write_i64(writer, msg_id);
	mpack_write_cstr(writer, request);
}

inline void MPackStartNotification(const char *notification, mpack_writer_t *writer) {
	mpack_start_array(writer, 3);
	mpack_write_i64(writer, static_cast<int64_t>(MPackMessageType::Notification));
	mpack_write_cstr(writer, notification);
}

[[nodiscard]] inline size_t MPackFinishMessage(mpack_writer_t *writer) {
	mpack_finish_array(writer);
	size_t size = mpack_writer_buffer_used(writer);
	mpack_error_t err = mpack_writer_destroy(writer);
	assert(err == mpack_ok);
	return size;
}

inline void MPackSendData(HANDLE handle, void *buffer, size_t size) {
	DWORD bytes_written;
	bool success = WriteFile(handle, buffer, static_cast<DWORD>(size), &bytes_written, nullptr);
	assert(success);
}

inline MPackMessageResult MPackExtractMessageResult(mpack_tree_t *tree) {
	mpack_node_t root = mpack_tree_root(tree);
	assert(mpack_node_array_at(root, 0).data->type == mpack_type_uint);

	MPackMessageType message_type = static_cast<MPackMessageType>(mpack_node_array_at(root, 0).data->value.i);
	if (message_type == MPackMessageType::Request) {
		assert(mpack_node_array_at(root, 1).data->type == mpack_type_uint);
		assert(mpack_node_array_at(root, 2).data->type == mpack_type_str);
		
		return MPackMessageResult {
			.type = message_type,
			.params = mpack_node_array_at(root, 3),
			.request = {
				.method = mpack_node_array_at(root, 2),
				.msg_id = mpack_node_array_at(root, 1).data->value.i
			}
		};
	}
	else if (message_type == MPackMessageType::Response) {
		assert(mpack_node_array_at(root, 1).data->type == mpack_type_uint);
		assert(mpack_node_array_at(root, 2).data->type == mpack_type_nil);

		return MPackMessageResult {
			.type = message_type,
			.params = mpack_node_array_at(root, 3),
			.response {
				.error = mpack_node_array_at(root, 2),
				.msg_id = mpack_node_array_at(root, 1).data->value.i
			}
		};
	}
	else if (message_type == MPackMessageType::Notification) {
		assert(mpack_node_array_at(root, 1).data->type == mpack_type_str);
		assert(mpack_node_array_at(root, 2).data->type == mpack_type_array);

		return MPackMessageResult {
			.type = message_type,
			.params = mpack_node_array_at(root, 2),
			.notification = {
				.name = mpack_node_array_at(root, 1)
			}
		};
	}

	return MPackMessageResult {};
}
