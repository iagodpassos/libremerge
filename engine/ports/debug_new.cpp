// SPDX-License-Identifier: GPL-3.0-or-later
// LibreMerge ports layer: definitions for the debug operator new/delete
// declared in Src/Common/DebugNew.h. Upstream gets these from the MFC debug
// heap; here they simply forward to the standard allocator.
#if !defined(_WIN32) && defined(_DEBUG)

#include <new>
#include <cstddef>

void* operator new(size_t size, const char *file, int line)
{
	(void)file; (void)line;
	return ::operator new(size);
}

void* operator new[](size_t size, const char *file, int line)
{
	(void)file; (void)line;
	return ::operator new[](size);
}

void operator delete(void* p, const char *file, int line)
{
	(void)file; (void)line;
	::operator delete(p);
}

void operator delete[](void* p, const char *file, int line)
{
	(void)file; (void)line;
	::operator delete[](p);
}

#endif // !_WIN32 && _DEBUG
