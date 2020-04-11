#ifndef __MM_INIT_HPP_INCLUDED__
#define __MM_INIT_HPP_INCLUDED__

#include <mm/kheap.hpp>
#include <mm/kvmmngr.hpp>
#include <mm/memoryinfo.hpp>
#include <boot/multiboot.hpp>
#include <mm/physalloc.hpp>
#include <mm/tmpphysalloc.hpp>
#include <mm/tmpvalloc.hpp>
#include <mm/vmmap.hpp>

namespace memory {
    INLINE void init(Uint64 mbPointer) {
        memory::BootMemoryInfo::init(mbPointer);
        memory::TempPhysAllocator::init();
        memory::TempVirtualAllocator::init(KERNEL_MAPPING_BASE + 128 MB);
        memory::PhysAllocator::init();
        memory::KernelVirtualAllocator::init();
        memory::KernelHeap::init();
    }
} // namespace memory

#endif