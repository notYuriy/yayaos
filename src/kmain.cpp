#include <core/interrupts.hpp>
#include <core/log.hpp>
#include <core/uniqueptr.hpp>
#include <drivers/pic/pic.hpp>
#include <drivers/pic/pic8259.hpp>
#include <drivers/serial.hpp>
#include <drivers/timer/pit.hpp>
#include <fs/ramdiskfs.hpp>
#include <fs/vfs.hpp>
#include <mm/kvmmngr.hpp>
#include <mm/mminit.hpp>
#include <mm/usrvmmngr.hpp>
#include <proc/elf.hpp>
#include <proc/mutex.hpp>
#include <proc/sched.hpp>

extern "C" void kmain(uint64_t mbPointer, void (**ctorsStart)(),
                      void (**ctorsEnd)()) {

    executeCtors(ctorsStart, ctorsEnd);
    drivers::Serial::init(drivers::SerialPort::COM1);
    memory::init(mbPointer);
    core::IDT::init();
    drivers::IPIC::detectPIC();
    drivers::PIT timer;
    timer.init(200);
    proc::Scheduler::init(&timer);
    // timer.enable();
    fs::RamdiskFsSuperblock initRd;
    fs::VFS::init(&initRd);
    core::UniquePtr<fs::IFile> file(fs::VFS::open("/bin/binaryExample", 0));
    core::UniquePtr<proc::Elf> elf = proc::parseElf(file.get());
}
