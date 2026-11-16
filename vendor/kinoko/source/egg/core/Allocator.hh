#pragma once

#include "egg/core/Heap.hh"

namespace Kinoko::EGG {

template <typename T>
struct Allocator {
    using value_type = T;

    Allocator() = default;
    template <typename U>
    constexpr Allocator(const Allocator<U> &) noexcept {}

    [[nodiscard]] T *allocate(size_t n) {
        return static_cast<T *>(egg_alloc(n * sizeof(T), static_cast<s32>(alignof(T))));
    }

    void deallocate(T *ptr, size_t) noexcept {
        egg_free(ptr);
    }
};

template <typename T, typename U>
constexpr bool operator==(const Allocator<T> &, const Allocator<U> &) noexcept {
    return true;
}

/// @brief mkw-3ds: Allocator that always takes memory from the heap itself, never from an
/// active BlockArena (for containers that outlive the arena's owner, such as the kart proxy
/// list, which is cleared after the karts are freed).
template <typename T>
struct HeapAllocator {
    using value_type = T;

    HeapAllocator() = default;
    template <typename U>
    constexpr HeapAllocator(const HeapAllocator<U> &) noexcept {}

    [[nodiscard]] T *allocate(size_t n) {
        return static_cast<T *>(Heap::alloc(n * sizeof(T), static_cast<s32>(alignof(T)), nullptr));
    }

    void deallocate(T *ptr, size_t) noexcept {
        egg_free(ptr);
    }
};

template <typename T, typename U>
constexpr bool operator==(const HeapAllocator<T> &, const HeapAllocator<U> &) noexcept {
    return true;
}

} // namespace Kinoko::EGG
