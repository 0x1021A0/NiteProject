#include <arch/x86_64/apic.h>
#include <arch/x86_64/arch.h>
#include <arch/x86_64/interrupts.h>
#include <arch/x86_64/paging.h>
#include <arch/x86_64/serial.h>
#include <arch/x86_64/smbios.h>
#include <arch/x86_64/types.h>
#include <siberix/drivers/acpi/acpi_device.h>
#include <siberix/drivers/pci/devices.h>
#include <siberix/mm/page.h>

#include <arch/x86_64/smpdefines.inc>

static SbrxkrnlX64Impl sbrxkrnl;
extern "C" void        _lgdt(u64);
extern "C" void        _lidt(u64);

extern void* smpTrampolineStart;
extern void* smpTrampolineEnd;

volatile u16* smpMagicValue      = (u16*)SMP_TRAMPOLINE_DATA_START_FLAG;
volatile u16* smpTrampolineCpuID = (u16*)SMP_TRAMPOLINE_CPU_ID;
GdtPtr*       smpGdtPtr          = (GdtPtr*)SMP_TRAMPOLINE_GDT_PTR;
volatile u64* smpRegisterCR3     = (u64*)SMP_TRAMPOLINE_CR3;
volatile u64* smpStack           = (u64*)SMP_TRAMPOLINE_STACK;
volatile u64* smpEntry2          = (u64*)SMP_TRAMPOLINE_ENTRY2;
volatile bool doneInit           = false;

TaskStateSegment              tss = { .rsp = {}, .ist = {}, .iopbOffset = 0 };
Paging::X64KernelAddressSpace addressSpace;
SegAlloc                      segAlloc;
BuddyAlloc                    buddyAlloc;
ApicDevice*                   _apic;

SiberixKernel* siberix() { return &sbrxkrnl; }

void trampolineStart(u16 cpuId) {
    SbrxkrnlX64Impl* sbrxkrnl = static_cast<SbrxkrnlX64Impl*>(siberix());
    Cpu*             cpu      = getCpuLocal();

    setCpuLocal(cpu);

    cpu->gdt = reinterpret_cast<GdtPackage*>(sbrxkrnl->getMemory().alloc4KPages(1));
    memcpy(cpu->gdt, (void*)sbrxkrnl->m_gdtPtr.base, sbrxkrnl->m_gdtPtr.limit + 1);
    cpu->gdtPtr = { .limit = sbrxkrnl->m_gdtPtr.limit, .base = (u64)cpu->gdt };
    cpu->idtPtr = { .limit = sbrxkrnl->m_idtPtr.limit, .base = (u64)sbrxkrnl->m_idtPtr.base };

    asm volatile("lgdt (%%rax)" ::"a"(&cpu->gdtPtr));
    asm volatile("lidt %0" ::"m"(cpu->idtPtr));

    cpu->tss.init(cpu->gdt);
    ApicDevice* apic =
        static_cast<ApicDevice*>(sbrxkrnl->getConnectivity()->findDevice("APIC Controller"));
    if (apic != nullptr) {
        apic->getInterface(cpuId).setup();
        Logger::getLogger("apic").success("CPU [%u] loaded", cpuId);
    }

    asm("sti");
    doneInit = true;

    for (;;) asm volatile("pause");
}

bool SbrxkrnlX64Impl::setupArch() {
    /* load global descriptor table */
    m_gdt    = GdtPackage(tss);
    m_gdtPtr = { .limit = sizeof(GdtPackage) - 1, .base = (u64)&m_gdt };
    _lgdt((u64)&m_gdtPtr);
    /* load interrupt descriptor table */
    for (int i = 0; i < IDT_ENTRY_COUNT; i++)
        idtEntryList[i] = IdtEntry(i, intTables[i], 0x08, IDT_FLAGS_INTGATE, 0);
    m_idtPtr = { .limit = sizeof(IdtEntry) * IDT_ENTRY_COUNT, .base = (u64)&idtEntryList };
    _lidt((u64)&m_idtPtr);

    // Initialize memory management
    // m_kernelSpace   = &(addressSpace = Paging::X64KernelAddressSpace());
    this->m_memory    = MemoryService();
    this->m_devices   = new DeviceConnectivity();
    this->m_scheduler = new Scheduler();

    m_cpus[0] = new Cpu{ .apicId        = 0,
                         .gdt           = &m_gdt,
                         .gdtPtr        = m_gdtPtr,
                         .idtPtr        = m_idtPtr,
                         .currentThread = getScheduler()->getKernelProcess()->getMainThread(),
                         .idleThread    = getProcessFactory()->createIdleThread() };
    m_cpus[0]->tss.init(&m_gdt);
    setCpuLocal(m_cpus[0]);

    (new SerialPortDevice())->initialize();    /* Serial Port */
    (new SmbiosDevice())->initialize();        /* System Management BIOS */
    (new AcpiPmDevice())->initialize();        /* ACPI Power Management */
    (new PciControllerDevice())->initialize(); /* PCI Controller */

    _apic = new ApicDevice(); /* APIC Controller */
    _apic->initialize();
    getConnectivity()->enumerateDevice(DeviceType::Processor).forEach([&](Device& device) -> void {
        u32 processorId = static_cast<ProcessorDevice&>(device).getProcessorId();
        if (processorId) {
            Logger::getLogger("hw").info("CPU [%u] is being initialized", processorId);

            ApicLocalInterface& interface = _apic->getInterface(processorId);

            *smpMagicValue      = 0;
            *smpTrampolineCpuID = processorId;
            *smpEntry2          = (u64)trampolineStart;
            *smpStack           = (u64)(m_memory.alloc4KPages(4)) + 16384;
            *smpGdtPtr          = m_gdtPtr;

            asm volatile("mov %%cr3, %%rax" : "=a"(*smpRegisterCR3));

            interface.sendInterrupt(ICR_DSH_DEST, ICR_MESSAGE_TYPE_INIT, 0);
            // Sleep 50 ms
            while (*smpMagicValue != 0xb33f) {
                interface.sendInterrupt(
                    ICR_DSH_DEST, ICR_MESSAGE_TYPE_STARTUP, (SMP_TRAMPOLINE_ENTRY >> 12));
                // Sleep 200 ms
            }

            while (!doneInit) asm("pause");
            Logger::getLogger("hw").info("CPU [%u] loaded", processorId);
            doneInit = false;
        }
    });

    return true;
}

void TaskStateSegment::init(GdtPackage* package) {
    package->tss = GdtTssEntry(*this);

    memset(this, 0, sizeof(TaskStateSegment));

    for (int i = 0; i < 3; i++) {
        ist[i] = (u64)siberix()->getMemory().alloc4KPages(8);
        memset((void*)ist[i], 0, PAGE_SIZE_4K);
        ist[i] += PAGE_SIZE_4K * 8;
    }

    asm volatile("mov %%rsp, %0" : "=r"(rsp[0]));
    asm volatile("ltr %%ax" ::"a"(0x28));
}