/* BGE-Remix, from BGE NEXT (CPU performance, 24 October 2026): every C++ new and delete in the executable goes
 * through mimalloc (WITH_MEM_MIMALLOC), unless BGE_REMIX_NO_MIMALLOC=1 switches the whole run back
 * to the system's allocator (MEM_use_mimalloc decides once, at the first allocation). Built into
 * the executable itself, once, so it replaces the C++ library's operators. */
#define MI_STATIC_LIB
#include <cstdlib>
#include <new>
#include "mimalloc.h"

extern "C" int MEM_use_mimalloc(void);

static inline void *sys_alloc(std::size_t n)
{
	return std::malloc(n ? n : 1);
}

static void *alloc_or_throw(std::size_t n)
{
	if (MEM_use_mimalloc())
		return mi_new(n);
	for (;;) {
		void *p = sys_alloc(n);
		if (p)
			return p;
		std::new_handler h = std::get_new_handler();
		if (!h)
			throw std::bad_alloc();
		h();
	}
}

static void *alloc_nothrow(std::size_t n) noexcept
{
	if (MEM_use_mimalloc())
		return mi_new_nothrow(n);
	try {
		return alloc_or_throw(n);
	}
	catch (...) {
		return nullptr;
	}
}

static inline void free_any(void *p) noexcept
{
	if (!p)
		return;
	if (MEM_use_mimalloc())
		mi_free(p);
	else
		std::free(p);
}

void *operator new(std::size_t n) { return alloc_or_throw(n); }
void *operator new[](std::size_t n) { return alloc_or_throw(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return alloc_nothrow(n); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return alloc_nothrow(n); }
void operator delete(void *p) noexcept { free_any(p); }
void operator delete[](void *p) noexcept { free_any(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { free_any(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { free_any(p); }
#if defined(__cpp_sized_deallocation) || defined(_MSC_VER)
void operator delete(void *p, std::size_t) noexcept { free_any(p); }
void operator delete[](void *p, std::size_t) noexcept { free_any(p); }
#endif

#if defined(__cpp_aligned_new) || (defined(_MSC_VER) && _MSVC_LANG >= 201703L)
static void *alloc_aligned(std::size_t n, std::align_val_t al)
{
	if (MEM_use_mimalloc())
		return mi_new_aligned(n, static_cast<std::size_t>(al));
#  ifdef _WIN32
	void *p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(al));
#  else
	void *p = nullptr;
	if (posix_memalign(&p, static_cast<std::size_t>(al) < sizeof(void *) ? sizeof(void *) : static_cast<std::size_t>(al), n ? n : 1) != 0)
		p = nullptr;
#  endif
	if (!p)
		throw std::bad_alloc();
	return p;
}
static void free_aligned(void *p) noexcept
{
	if (!p)
		return;
	if (MEM_use_mimalloc())
		mi_free(p);
	else
#  ifdef _WIN32
		_aligned_free(p);
#  else
		std::free(p);
#  endif
}
void *operator new(std::size_t n, std::align_val_t al) { return alloc_aligned(n, al); }
void *operator new[](std::size_t n, std::align_val_t al) { return alloc_aligned(n, al); }
void *operator new(std::size_t n, std::align_val_t al, const std::nothrow_t &) noexcept
{
	try { return alloc_aligned(n, al); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t &) noexcept
{
	try { return alloc_aligned(n, al); } catch (...) { return nullptr; }
}
void operator delete(void *p, std::align_val_t) noexcept { free_aligned(p); }
void operator delete[](void *p, std::align_val_t) noexcept { free_aligned(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { free_aligned(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { free_aligned(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { free_aligned(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { free_aligned(p); }
#endif
