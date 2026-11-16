#include "egg/core/ExpHeap.hh"

using namespace Kinoko;
using namespace Abstract::Memory;

namespace Kinoko::EGG {

/// @addr{0x802296E8}
Heap::Heap(MEMiHeapHead *handle) : m_handle(handle), m_children(Disposer::getLinkOffset()) {
    m_block = nullptr;
    m_parentHeap = nullptr;
    m_name = "NoName";
    m_flags.makeAllZero();

    s_heapList.append(this);
}

/// @addr{0x80229780}
Heap::~Heap() {
    s_heapList.remove(this);
}

/// @addr{0x80229C5C}
void Heap::dispose() {
    Disposer *node = nullptr;
    while ((node = reinterpret_cast<Disposer *>(m_children.getFirst()))) {
        node->~Disposer();
    }

    ASSERT(!m_children.m_headObject && !m_children.m_tailObject);
}

Heap *Heap::becomeAllocatableHeap() {
    Heap *oldHeap = s_allocatableHeap;
    s_allocatableHeap = this;
    return oldHeap;
}

/// @addr{0x80229D74}
Heap *Heap::becomeCurrentHeap() {
    Heap *oldHeap = s_currentHeap;
    s_currentHeap = this;
    return oldHeap;
}

/// @addr{0x80229814}
void *Heap::alloc(size_t size, int align, Heap *pHeap) {
    Heap *currentHeap = s_currentHeap;

    if (s_allocatableHeap) {
        if (currentHeap && !pHeap) {
            pHeap = currentHeap;
        }

        if (pHeap != s_allocatableHeap) {
            WARN("HEAP ALLOC FAIL (%p, %s): Allocatable heap is %p (%s)", pHeap, pHeap->getName(),
                    s_allocatableHeap, s_allocatableHeap->getName());

            return nullptr;
        }
    }

    if (pHeap) {
        return pHeap->alloc(size, align);
    }

    if (currentHeap) {
        void *block = currentHeap->alloc(size, align);

        if (!block) {
            u32 heapFreeSize = currentHeap->getAllocatableSize(0x4);
            s32 heapSize = GetAddrNum(currentHeap->getEndAddress()) -
                    GetAddrNum(currentHeap->getStartAddress());

            constexpr f32 BYTES_TO_MBYTES = 1024.0f * 1024.0f;
            f32 heapSizeMB = static_cast<f32>(heapSize) / BYTES_TO_MBYTES;
            f32 heapFreeSizeMB = static_cast<f32>(heapFreeSize) / BYTES_TO_MBYTES;
            f32 sizeMB = static_cast<f32>(size) / BYTES_TO_MBYTES;

            WARN("HEAP ALLOC FAIL (%p, %s):\nTotal bytes: %d (%.1fMBytes)\nFree bytes: %d "
                 "(%.1fMBytes)\nAlloc bytes: %zu "
                 "(%.1fMBytes)\nAlign: %d",
                    currentHeap, currentHeap->getName(), heapSize, static_cast<f64>(heapSizeMB),
                    heapFreeSize, static_cast<f64>(heapFreeSizeMB), size, static_cast<f64>(sizeMB),
                    align);
        }

        return block;
    }

    WARN("HEAP ALLOC FAIL: Cannot allocate %zu from heap %p", size, pHeap);
    return nullptr;
}

/// @addr{0x80229B84}
void Heap::free(void *block, Heap *pHeap) {
    // Deleting nullptr should be no-op
    if (!block) {
        return;
    }

    if (!pHeap) {
        MEMiHeapHead *handle = MEMiHeapHead::findContainHeap(block);
        if (!handle) {
            return;
        }

        pHeap = findHeap(handle);
        if (!pHeap) {
            return;
        }
    }

    pHeap->free(block);
}

Heap *Heap::findHeap(MEMiHeapHead *handle) {
    Heap *node = nullptr;
    while ((node = reinterpret_cast<Heap *>(s_heapList.getNext(node)))) {
        if (node->m_handle == handle) {
            return node;
        }
    }

    return nullptr;
}

/// @addr{0x80229ADC}
Heap *Heap::findContainHeap(const void *block) {
    MEMiHeapHead *handle = MEMiHeapHead::findContainHeap(block);
    return handle ? findHeap(handle) : nullptr;
}

BlockArena *BlockArena::Open(size_t capacity) {
    for (auto &arena : s_arenas) {
        if (arena.m_start) {
            continue;
        }

        void *block = Heap::alloc(capacity, 32, nullptr);
        if (!block) {
            return nullptr;
        }

        arena.m_start = static_cast<u8 *>(block);
        arena.m_cur = arena.m_start;
        arena.m_end = arena.m_start + capacity;
        ++s_open;
        return &arena;
    }

    return nullptr;
}

BlockArena *BlockArena::Containing(const void *p) {
    const u8 *at = static_cast<const u8 *>(p);
    for (auto &arena : s_arenas) {
        if (arena.m_start && at >= arena.m_start && at < arena.m_end) {
            return &arena;
        }
    }

    return nullptr;
}

void BlockArena::ForgetAll() {
    for (auto &arena : s_arenas) {
        arena.m_start = arena.m_cur = arena.m_end = nullptr;
    }

    s_active = nullptr;
    s_open = 0;
}

void BlockArena::trim(size_t reserve) {
    const size_t keep = RoundUp(used() + reserve, 4);
    if (!m_start || keep >= size()) {
        return;
    }

    ExpHeap *heap = Heap::dynamicCastToExp(Heap::findContainHeap(m_start));
    if (heap) {
        m_end = m_start + heap->shrinkBlock(m_start, keep);
    }
}

void BlockArena::release() {
    void *block = m_start;
    if (!block) {
        return;
    }

    if (s_active == this) {
        s_active = nullptr;
    }

    m_start = m_cur = m_end = nullptr;
    --s_open;
    Heap::free(block, nullptr);
}

void *BlockArena::Alloc(size_t size, s32 align) {
    BlockArena *arena = s_active;

    // As MEMiExpHeapHead::alloc: at least 1 byte, sizes in whole words, 4-byte alignment.
    size = RoundUp(size == 0 ? 1 : size, 4);
    const uintptr_t at = RoundUp(GetAddrNum(arena->m_cur), static_cast<uintptr_t>(align < 4 ? 4 : align));
    if (at > GetAddrNum(arena->m_end) || size > GetAddrNum(arena->m_end) - at) {
        ++s_fallbacks;
        return nullptr;
    }

    arena->m_cur = static_cast<u8 *>(GetAddrPtr(at + size));
    ++s_allocs;
    return GetAddrPtr(at);
}

void *egg_alloc(size_t size, s32 align, Heap *pHeap) {
    // mkw-3ds: an active BlockArena serves allocations of the current heap.
    if (BlockArena::Active() && !pHeap && align > 0) {
        if (void *block = BlockArena::Alloc(size, align)) {
            return block;
        }
    }

    return Heap::alloc(size, align, pHeap);
}

void egg_free(void *block, Heap *pHeap) {
    // mkw-3ds: a BlockArena's allocations go back to the heap with its block.
    if (BlockArena::Owns(block)) {
        return;
    }

    Heap::free(block, pHeap);
}

BlockArena BlockArena::s_arenas[BlockArena::MAX_ARENAS];
BlockArena *BlockArena::s_active = nullptr;
u32 BlockArena::s_open = 0;
u32 BlockArena::s_allocs = 0;
u32 BlockArena::s_fallbacks = 0;

} // namespace Kinoko::EGG

MEMList EGG::Heap::s_heapList = MEMList(EGG::Heap::getOffset()); ///< @addr{0x80384320}

EGG::Heap *EGG::Heap::s_currentHeap = nullptr;     ///< @addr{0x80386EA0}
EGG::Heap *EGG::Heap::s_allocatableHeap = nullptr; ///< @addr{0x80386EA8}
