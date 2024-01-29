#include <siberix/main.h>
#include <siberix/mem/kmem-alloc.h>

Kern::Mem::IMemAlloc* _kernMemAlloc;

namespace Kern::Mem {
    KernMemAlloc::KernMemAlloc()
      : m_poolSizes({ 8, 16, 24, 32, 64, 96, 128, 192, 256, 512, 1024, 2048 })
    {
        _kernMemAlloc = this;
        for (uint64_t i = 0; i < 12; i++) {
            m_pools[i] = (KernMemAlloc::MemPool*)Main::alloc4KPages(1);
        }
    }

    uint64_t KernMemAlloc::alloc(uint64_t size)
    {
        return 0;
    }

    void KernMemAlloc::free(void* address) {}
}