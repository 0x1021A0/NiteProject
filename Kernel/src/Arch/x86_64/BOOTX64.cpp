#include <Arch/x86_64/BOOTX64.h>
#include <Arch/x86_64/ACPI.h>
#include <Arch/x86_64/IRQ.h>
#include <Arch/x86_64/CPU.h>
#include <Arch/x86_64/GDT.h>
#include <Arch/x86_64/IDT.h>
#include <Arch/x86_64/PIC.h>
#include <Arch/x86_64/PCI.h>
#include <Arch/x86_64/SMBios.h>
#include <Arch/x86_64/SMP.h>
#include <Init/BootInfo.h>

using namespace Firmware;

namespace Boot
{
    using namespace Firmware;
    static BootInfo g_BootInfo;

    void multiboot2(BootInfo *info, multiboot2_info_header_t *mbInfo)
    {
        
    }

    void stivale2(BootInfo *info, stivale2_struct_t *stInfo)
    {
        struct stivale2_tag* tag = (struct stivale2_tag*)(stInfo->tags);
        BootInfoMemory *memInfo = &bootInfo->mem;
        while (tag)
        {
            switch (tag->identifier)
            {
            case STIVALE2_STRUCT_TAG_MEMMAP_ID:
            {
                struct stivale2_struct_tag_memmap *st2_mem_tag = (struct stivale2_struct_tag_memmap*)(tag);
                for (uint64_t idx = 0; idx < st2_mem_tag->entries; idx++)
                {
                    struct stivale2_mmap_entry *fromEntry = &st2_mem_tag->memmap[idx];
                    MemoryMapEntry *newEntry = &memInfo->m_MemoryMapEntries[memInfo->m_MemoryMapSize];

                    if (fromEntry->base > UINTPTR_MAX ||
                        fromEntry->base + fromEntry->length > UINTPTR_MAX)
                    {
                        continue;
                    }

                    memInfo->m_TotalSize += fromEntry->length;
                    newEntry->range.start = fromEntry->base;
                    newEntry->range.end = fromEntry->base + fromEntry->length;
                    switch (fromEntry->type)
                    {
                    case STIVALE2_MMAP_USABLE:
                        memInfo->m_Usable += fromEntry->length;
                        newEntry->m_Type = 0;
                        break; 
                    case STIVALE2_MMAP_KERNEL_AND_MODULES:
                        newEntry->m_Type = 5;
                        break;
                    case STIVALE2_MMAP_ACPI_RECLAIMABLE:
                        newEntry->m_Type = 2;
                        break;
                    case STIVALE2_MMAP_ACPI_NVS:
                        newEntry->m_Type = 3;
                        break;
                    case STIVALE2_MMAP_BAD_MEMORY:
                        newEntry->m_Type = 4;
                        break;
                    default:
                        newEntry->m_Type = 1;
                        break;
                    }
                    memInfo->map_size++;
                }
                break;
            }
            case STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID:
            {
                struct stivale2_struct_tag_framebuffer *framebuffer_tag = (struct stivale2_struct_tag_framebuffer *) tag;
                BootInfoGraphics *graphic = &bootInfo->m_Graphics;

                graphic->m_Width = framebuffer_tag->framebuffer_width;
                graphic->m_Height = framebuffer_tag->framebuffer_height;

                graphic->m_BufferAddress = framebuffer_tag->framebuffer_addr;
                graphic->m_Pitch = framebuffer_tag->framebuffer_pitch;
                graphic->m_BytesPerPixel = framebuffer_tag->framebuffer_bpp;
                break;
            }
            case STIVALE2_STRUCT_TAG_MODULES_ID:
            {
                break;
            }
            case STIVALE2_STRUCT_TAG_RSDP_ID:
            {
                break;
            }
            }
            tag = (struct stivale2_tag*)(tag->next);
        }

        bootInfo->check = 0xDEADC0DE;
    }

    void Start()
    {
        if(g_BootInfo.m_Checksum != 0xDEADC0DE)
        {
            return;
        }

        DisableInterrupts();

        GDT::Initialize();
        IDT::Initialize();

        Memory::Initialize();

        PIC::Initialize();
        PIT::Initialize();

        EnableInterrupts();

        CPUIDInfo cpuId = CPUID();

        ACPI::Initialize();
        // APIC
        if(cpuId.edx & CPUID_EDX_APIC)
        {
            PIC::Disable();
            APIC::Local::Initialize();
            WriteLine("[Local APIC] OK!");

            APIC::IO::Initialize();
            WriteLine("[I/O APIC] OK!");
        }
        else
            WriteLine("[APIC] Not Present.");

        SMBios::Initialize();
        SMP::Initialize();
    }
} // namespace Boot

extern "C" [[noreturn]] void kload_stivale2(void *ptr)
{
    if(addr == NULL)
        __asm__("mov $0x32, %al");

    Boot::stivale2(
        &Boot::g_BootInfo,
        (stivale2_struct_t*)(addr)
    );
    Boot::Start();

hang:
    __asm__("hlt");
    goto hang;
}