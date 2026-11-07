// Debug allocation guard (ARCHITECTURE.md section 4). Replaces global operator new in
// the test binary only and counts any allocation made inside an rt::ScopedRealtime
// section (Engine::process, EngineHost::process). Tests assert the count stays zero.
#include "AllocGuard.h"
#include "core/RealtimeGuard.h"
#include <atomic>
#include <cstdlib>
#include <new>

namespace spsx::test
{
    static std::atomic<long> violations { 0 };
    long realtimeAllocations() { return violations.load(); }
    void resetRealtimeAllocations() { violations.store (0); }
}

static void* guardedAlloc (std::size_t size)
{
    if (spsx::rt::inRealtimeSection())
        spsx::test::violations.fetch_add (1);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

static void* guardedAlignedAlloc (std::size_t size, std::align_val_t align)
{
    if (spsx::rt::inRealtimeSection())
        spsx::test::violations.fetch_add (1);
    const auto a = static_cast<std::size_t> (align);
    const std::size_t rounded = ((size == 0 ? 1 : size) + a - 1) / a * a;
    if (void* p = std::aligned_alloc (a, rounded))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t s)                                    { return guardedAlloc (s); }
void* operator new[] (std::size_t s)                                  { return guardedAlloc (s); }
void* operator new (std::size_t s, const std::nothrow_t&) noexcept    { try { return guardedAlloc (s); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t s, const std::nothrow_t&) noexcept  { try { return guardedAlloc (s); } catch (...) { return nullptr; } }
void* operator new (std::size_t s, std::align_val_t a)                { return guardedAlignedAlloc (s, a); }
void* operator new[] (std::size_t s, std::align_val_t a)              { return guardedAlignedAlloc (s, a); }
void operator delete (void* p) noexcept                               { std::free (p); }
void operator delete[] (void* p) noexcept                             { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                  { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept             { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept           { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept   { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
