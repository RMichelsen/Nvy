#pragma once
#include <cstdint>
#include <windows.h>

constexpr uint32_t PAGE_SIZE = 0x1000;
constexpr size_t MEGABYTES(size_t n) {
	return n * 1024 * 1024;
}

// A heap-allocated vector, reserves 1GB of virtual memory,
// commits as necessary. Ensures no reallocations.
constexpr size_t VEC_MAX_SIZE = MEGABYTES(1024);
template<typename T>
struct Vec {
	T *data_begin;
	T *data_end;
	T *alloc_end;

	Vec() {
		data_begin = reinterpret_cast<T *>(VirtualAlloc(nullptr, VEC_MAX_SIZE, MEM_RESERVE, PAGE_NOACCESS));
		data_end = data_begin;
		VirtualAlloc(data_begin, PAGE_SIZE * 4, MEM_COMMIT, PAGE_READWRITE);
		alloc_end = reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(data_begin) + PAGE_SIZE * 4);
	}

	~Vec() {
		VirtualFree(data_begin, 0, MEM_RELEASE);
	}

	inline T operator[](size_t i) const {
		return *(data_begin + i);
	}
	inline T &operator[](size_t i) {
		return *(data_begin + i);
	}

	inline T *data() {
		return data_begin;
	}

	inline size_t size() {
		return static_cast<size_t>(data_end - data_begin);
	}

	inline size_t capacity() {
		return static_cast<size_t>(alloc_end - data_begin);
	}

	inline void push_back(const T &item) {
		if (capacity() <= size()) {
			grow();
		}
		*data_end++ = item;
	}

	inline void push_back(T &&item) {
		if (capacity() <= size()) {
			grow();
		}
		*data_end++ = item;
	}

	inline void resize(size_t new_size) {
		while (alloc_end <= data_begin + new_size) {
			grow();
		}

		data_end = data_begin + new_size;
	}

	inline void grow() {
		size_t byte_capacity = reinterpret_cast<uint8_t *>(alloc_end) - reinterpret_cast<uint8_t *>(data_begin);
		VirtualAlloc(alloc_end, byte_capacity, MEM_COMMIT, PAGE_READWRITE);
		alloc_end = reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(alloc_end) + byte_capacity);
	}

	inline void clear() {
		uint64_t byte_capacity = reinterpret_cast<uint8_t *>(alloc_end) - reinterpret_cast<uint8_t *>(data_begin);
		VirtualAlloc(data_begin, byte_capacity, MEM_RESET, PAGE_NOACCESS);
		data_end = data_begin;
		VirtualAlloc(data_begin, PAGE_SIZE * 4, MEM_COMMIT, PAGE_READWRITE);
		alloc_end = reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(data_begin) + PAGE_SIZE * 4);
	}

	using iterator = T *;
	using const_iterator = T *const;
	inline iterator begin() {
		return data_begin;
	}
	inline iterator end() {
		return data_end;
	}
	inline const_iterator begin() const {
		return data_begin;
	}
	inline const_iterator end() const {
		return data_end;
	}
};