#pragma once
#include <cassert>
#include "mpack/mpack.h"

inline bool MPackMatchString(mpack_node_t node, const char *str_to_match) {
	assert(node.data->type == mpack_type_str);
	const char *str = mpack_node_str(node);
	int strlen = static_cast<int>(mpack_node_strlen(node));
	return strncmp(str, str_to_match, strlen) == 0;
}