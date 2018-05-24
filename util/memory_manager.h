#pragma once

#include <cstddef>
#include <cstdlib>

// linux-specific (check for _msize for windows and malloc_size on OSX)
#include <malloc.h>

#if defined(JEMALLOC) && JEMALLOC
#include <jemalloc/jemalloc.h>
#endif


#include <iostream>

struct memory {
	static void* allocate(std::size_t size) { return malloc(size); }
	static void* allocate(std::size_t requested_size, std::size_t& actual_size) {
		void* ptr = malloc(requested_size);
		actual_size = malloc_usable_size(ptr);
		return ptr;
	}
	static void* reallocate(void* ptr, std::size_t size) { return realloc(ptr, size); }
	static void* reallocate(void* ptr, std::size_t requested_size, std::size_t& actual_size) {
		ptr = realloc(ptr, requested_size);
		actual_size = malloc_usable_size(ptr);
		return ptr;
	}
	static void deallocate(void* ptr) { free(ptr); }
	static void deallocate(void* ptr, std::size_t const size) {
		#if defined(JEMALLOC) && JEMALLOC
			sdallocx(ptr, size, 0);
		#else
			static_cast<void>(size);
			free(ptr);
		#endif
	}
};
