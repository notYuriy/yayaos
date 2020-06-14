#include <fs/vfs.hpp>
#include <memory/cow.hpp>
#include <memory/kvmmngr.hpp>
#include <memory/msecurity.hpp>
#include <memory/vmbase.hpp>
#include <proc/descriptor.hpp>
#include <proc/elf.hpp>
#include <proc/proc.hpp>
#include <proc/stackpool.hpp>
#include <proc/userapi.hpp>
#include <proc/usermode.hpp>

namespace proc {
    [[noreturn]] void YY_ExitProcess() {
        core::log("YY_ExitProcess();\n\r");
        proc::ProcessManager::exit();
    }

    extern "C" int64_t YY_ConsoleWrite(char *location, uint64_t size) {
        core::log("YY_ConsoleWrite(%p, %llu);\n\r", location, size);
        if (size > YY_ConsoleOperationsSizeLimit) {
            return -1;
        }
        if (!memory::virtualRangeConditionCheck((memory::vaddr_t)location, size,
                                                true, false, false)) {
            return -1;
        }
        core::putsn(location, size);
        return 0;
    }

    extern "C" int64_t YY_GetSystemInfo(YY_SystemInfo *info) {
        if (!memory::virtualRangeConditionCheck((memory::vaddr_t)info,
                                                sizeof(YY_SystemInfo), true,
                                                true, false)) {
            return -1;
        }
        core::log("YY_GetSystemInfo(%p);\n\r", info);
        memset(info, sizeof(YY_SystemInfo), '\0');

        static const char kernelName[] = "YYSloth";
        static const char kernelRelease[] = "0.0.1";
        static const char kernelVersion[] = "#1-YayOS " __TIME__ " " __DATE__;
        static const char arch[] = "amd64";
        static const char operatingSystem[] = "YayOS";
        memcpy(&(info->kernelName), kernelName, sizeof(kernelName));
        memcpy(&(info->kernelRelease), kernelRelease, sizeof(kernelRelease));
        memcpy(&(info->kernelVersion), kernelVersion, sizeof(kernelVersion));
        memcpy(&(info->machine), arch, sizeof(arch));
        memcpy(&(info->processor), arch, sizeof(arch));
        memcpy(&(info->hardwarePlatform), arch, sizeof(arch));
        memcpy(&(info->operatingSystem), operatingSystem,
               sizeof(operatingSystem));
        return 0;
    }

    extern "C" void sysForkWithFrame(SchedulerIntFrame *frame) {
        core::log("YY_DuplicateProcess();\n\r");
        pid_t newProcessID = ProcessManager::newProcess();
        if (newProcessID == PID_MAX) {
            frame->rax = (uint64_t)(-1);
            return;
        }
        Process *newProc = ProcessManager::getProcessData(newProcessID);
        Process *currentProc = ProcessManager::getRunningProcess();
        if (!newProc->setup(false)) {
            ProcessManager::freePid(newProcessID);
            frame->rax = (uint64_t)(-1);
            return;
        }
        if ((newProc->usralloc = currentProc->usralloc->copy()) == nullptr) {
            ProcessManager::freePid(newProcessID);
            newProc->cleanup();
            StackPool::pushStack(newProc->kernelStackBase);
            frame->rax = (uint64_t)(-1);
            return;
        }
        newProc->pid = newProcessID;
        newProc->ppid = currentProc->pid;
        newProc->dead = 0;
        frame->rax = 0;
        newProc->state.generalRegs.copyFrom(frame);
        newProc->state.generalRegs.cr3 = memory::CoW::clonePageTable();
        newProc->descriptors = nullptr;
        if (newProc->state.generalRegs.cr3 == 0) {
            ProcessManager::freePid(newProcessID);
            newProc->cleanup();
            StackPool::pushStack(newProc->kernelStackBase);
            frame->rax = (uint64_t)(-1);
            return;
        }
        newProc->descriptors = new core::DynArray<DescriptorHandle *>;
        if (newProc->descriptors == nullptr) {
            ProcessManager::freePid(newProcessID);
            newProc->cleanup();
            StackPool::pushStack(newProc->kernelStackBase);
            frame->rax = (uint64_t)(-1);
            return;
        }
        for (uint64_t i = 0; i < currentProc->descriptors->size(); ++i) {
            if (!(newProc->descriptors->pushBack(
                    (*(currentProc->descriptors))[i]->clone()))) {
                ProcessManager::freePid(newProcessID);
                newProc->cleanup();
                StackPool::pushStack(newProc->kernelStackBase);
                frame->rax = (uint64_t)(-1);
                return;
            }
        }
        uint64_t stackOffset = currentProc->kernelStackTop - frame->rsp;
        uint64_t newStackLocation = newProc->kernelStackTop - stackOffset;
        newProc->state.extendedRegs.loadFromFPU();
        memcpy((void *)newStackLocation, (void *)frame->rsp, stackOffset);
        newProc->state.generalRegs.rsp = newStackLocation;
        frame->rax = newProcessID;
        ProcessManager::addToRunList(newProcessID);
    }

    extern "C" void YY_Yield() {
        core::log("YY_Yield();\n\r");
        proc::ProcessManager::yield();
    }

    extern "C" int64_t YY_QueryAPIInfo(uint64_t id) {
        core::log("YY_QueryAPIInfo(%llu);\n\r", id);
        switch (id) {
        case YY_APIInfoId_PageSize:
            return 0x1000;
        case YY_APIInfoID_MaxArgCount:
            return YY_MaxArgCount;
        case YY_APIInfoId_MaxArgLength:
            return YY_MaxArgLength;
        case YY_APIInfoID_ExecMaxPathLength:
            return YY_ExecMaxPathLength;
        case YY_APIInfoID_MaxFileIOBufSize:
            return YY_MaxFileIOBufSize;
        case YY_APIInfoID_MaxOpenFilePathLength:
            return YY_MaxOpenFilePath;
        default:
            // API Info at this id is not supported
            return (uint64_t)(-1);
        }
    }

    extern "C" int64_t YY_VirtualAlloc(uint64_t pagesCount, uint64_t flags) {
        core::log("YY_VirtualAlloc(%llu, %llu);\n\r", pagesCount, flags);
        Process *proc = proc::ProcessManager::getRunningProcess();
        memory::vaddr_t result = proc->usralloc->alloc(pagesCount * 0x1000);
        if (result == 0) {
            return -1;
        }
        uint64_t mask = (1ULL << 0) | (1ULL << 2);
        if ((flags & YY_VirtualFlagsWritable) != 0) {
            mask |= (1ULL << 1);
        }
        if ((flags & YY_VirtualFlagsExecutable) == 0) {
            mask |= (1ULL << 63);
        }
        if (!memory::VirtualMemoryMapper::mapPages(
                result, result + pagesCount * 0x1000, 0, mask)) {
            proc->usralloc->free(result, pagesCount * 0x1000);
            return 0;
        }
        return (int64_t)result;
    }

    extern "C" int64_t YY_VirtualFree(uint64_t start, uint64_t pagesCount) {
        core::log("YY_VirtualFree(%p, %llu)\n\r", start, pagesCount);
        if (pagesCount == 0) {
            return -1;
        }
        if (!memory::virtualRangeConditionCheck(start, pagesCount * 0x1000,
                                                false, false, false)) {
            return -1;
        }
        memory::VirtualMemoryMapper::freePages(start, pagesCount * 0x1000);
        Process *proc = proc::ProcessManager::getRunningProcess();
        if (!(proc->usralloc->free(start, pagesCount * 0x1000))) {
            // not enough memory to free the memory
            // that is unfortunate
            proc::ProcessManager::exit();
        }
        return 1;
    }

    extern "C" int64_t YY_CheckProcStatus(uint64_t pid) {
        // core::log("YY_CheckProcStatus(%llu)\n\r", pid);
        Process *proc = ProcessManager::getProcessData(pid);
        if (proc->ppid != ProcessManager::getRunningProcess()->pid) {
            return -1;
        }
        uint64_t dead = proc->dead;
        if (dead == 1) {
            ProcessManager::freePid(pid);
        }
        return (int64_t)dead;
    }

    extern "C" int64_t YY_ExecuteBinary(const char *path, uint64_t argc,
                                        const char **argv) {
        core::log("YY_ExecuteBinary(%p, %llu, %p);\n\r", path, argc, argv);
        if (!memory::validateCString(path, true, false, false,
                                     YY_ExecMaxPathLength)) {
            return -1;
        }
        if (!memory::virtualRangeConditionCheck(
                (memory::vaddr_t)argv, 8 * (argc + 1), true, false, false)) {
            return -1;
        }
        Process *proc = ProcessManager::getRunningProcess();
        fs::IFile *binary = nullptr;
        Elf *elf = nullptr;
        bool recovarable = true;

        if (argc > YY_MaxArgCount) {
            return -1;
        }
        for (uint64_t i = 0; i < argc; ++i) {
            if (!memory::validateCString(path, true, false, false,
                                         YY_MaxArgLength)) {
                return -1;
            }
        }

        char **argsTmp = new char *[argc + 1];
        memory::vaddr_t argsStorage = 0;
        char **argsPointers = nullptr;
        char *argsValues = nullptr;

        memset(argsTmp, sizeof(char *) * (argc + 1), '\0');
        uint64_t fullsizeof = 8 * (argc + 1);
        for (uint64_t i = 0; i < argc; ++i) {
            fullsizeof += (strlen(argv[i]) + 1);
            argsTmp[i] = strdup(argv[i]);
            if (argsTmp[i] == 0) {
                goto failureArgsCleanup;
            }
        }
        fullsizeof = alignUp(fullsizeof, 0x1000);
        argsTmp[argc] = NULL;

        binary = fs::VFS::open(path, false);
        if (binary == nullptr) {
            goto failureArgsCleanup;
        }
        proc->cleanup();
        recovarable = false;
        proc->usralloc = memory::newUserVirtualAllocator();
        proc->descriptors = new core::DynArray<DescriptorHandle *>;
        if (proc->usralloc == nullptr) {
            goto failureFileCleanup;
        }
        elf = parseElf(binary);
        if (elf == nullptr || proc->descriptors == nullptr) {
            goto failureUsrallocCleanup;
        }
        if (!elf->load(binary, proc->usralloc)) {
            goto failureElfCleanup;
        }
        argsStorage = proc->usralloc->alloc(fullsizeof);
        if (argsStorage == 0) {
            goto failureElfCleanup;
        }
        if (!memory::VirtualMemoryMapper::mapPages(
                argsStorage, argsStorage + fullsizeof, 0, (0x7))) {
            goto failureElfCleanup;
        }
        argsPointers = (char **)argsStorage;
        argsValues = (char *)(argsStorage + (sizeof(8) * (argc + 1)));
        for (uint64_t i = 0; i < argc; ++i) {
            argsPointers[i] = argsValues;
            uint64_t spaceUsed = strlen(argsTmp[i]) + 1;
            memcpy(argsPointers[i], argsTmp[i], spaceUsed);
            argsValues += spaceUsed;
        }
        delete binary;
        delete elf;
        for (uint64_t i = 0; i < argc; ++i) {
            if (argsTmp[i] != 0) {
                delete argsTmp[i];
            }
        }
        delete argsTmp;
        jumpToUserMode(elf->head.entryPoint, argc, argsPointers);
    failureElfCleanup:
        delete elf;
    failureUsrallocCleanup:
        proc->cleanup();
    failureFileCleanup:
        delete binary;
    failureArgsCleanup:
        for (uint64_t i = 0; i < argc; ++i) {
            if (argsTmp[i] != 0) {
                delete argsTmp[i];
            }
        }
        delete argsTmp;
        if (recovarable) {
            return (uint64_t)(-1);
        } else {
            proc::ProcessManager::exit();
        }
    }

    extern "C" int64_t YY_OpenFile(const char *path, bool writable) {
        if (!memory::validateCString(path, true, false, false,
                                     YY_MaxOpenFilePath)) {
            core::log("Here $1\n\r");
            return -1;
        }
        Process *proc = proc::ProcessManager::getRunningProcess();
        int64_t ind = proc->descriptors->size();
        for (int64_t i = 0; i < (int64_t)(proc->descriptors->size()); ++i) {
            if (proc->descriptors->at(ind) == nullptr) {
                ind = i;
                break;
            }
        }
        bool newElem = false;
        if (ind == (int64_t)(proc->descriptors->size())) {
            if (!(proc->descriptors->pushBack(nullptr))) {
                return -1;
            }
            newElem = true;
        }
        fs::IFile *file = fs::VFS::open(path, writable);
        if (file == nullptr) {
            if (newElem) {
                proc->descriptors->popBack();
            }
            return -1;
        }
        DescriptorHandle *handle = new DescriptorHandle(file);
        if (handle == nullptr) {
            core::log("Here\n\r");
            delete file;
            if (newElem) {
                proc->descriptors->popBack();
            }
            return -1;
        }
        proc->descriptors->at(ind) = handle;
        return ind;
    }

    extern "C" int64_t YY_ReadFile(int64_t fd, char *buf, int64_t size) {
        if (size > YY_MaxFileIOBufSize) {
            return -1;
        }
        if (!memory::virtualRangeConditionCheck(
                (memory::vaddr_t)buf, (uint64_t)size, true, true, false)) {
            return -1;
        }
        Process *proc = proc::ProcessManager::getRunningProcess();
        if (fd >= (int64_t)(proc->descriptors->size()) || fd < 0) {
            return -1;
        }
        proc->descriptors->at(fd)->mutex->lock();
        IDescriptor *desc = proc->descriptors->at(fd)->val;
        int64_t result = desc->read(size, (uint8_t *)buf);
        proc->descriptors->at(fd)->mutex->unlock();
        return result;
    }

    extern "C" int64_t YY_WriteFile(int64_t fd, const char *buf, int64_t size) {
        if (size > YY_MaxFileIOBufSize) {
            return -1;
        }
        if (!memory::virtualRangeConditionCheck(
                (memory::vaddr_t)buf, (uint64_t)size, true, false, false)) {
            return -1;
        }
        Process *proc = proc::ProcessManager::getRunningProcess();
        if (fd >= (int64_t)(proc->descriptors->size()) || fd < 0) {
            return -1;
        }
        proc->descriptors->at(fd)->mutex->lock();
        IDescriptor *desc = proc->descriptors->at(fd)->val;
        int64_t result = desc->write(size, (uint8_t *)buf);
        proc->descriptors->at(fd)->mutex->unlock();
        return result;
    }

    extern "C" int64_t YY_GetFilePos(int64_t fd) {
        Process *proc = proc::ProcessManager::getRunningProcess();
        if (fd >= (int64_t)(proc->descriptors->size()) || fd < 0) {
            return -1;
        }
        proc->descriptors->at(fd)->mutex->lock();
        IDescriptor *desc = proc->descriptors->at(fd)->val;
        int64_t result = desc->ltellg();
        proc->descriptors->at(fd)->mutex->unlock();
        return result;
    }

    extern "C" int64_t YY_SetFilePos(int64_t fd, int64_t offset,
                                     int64_t whence) {
        Process *proc = proc::ProcessManager::getRunningProcess();
        if (fd >= (int64_t)(proc->descriptors->size()) || fd < 0) {
            return -1;
        }
        proc->descriptors->at(fd)->mutex->lock();
        IDescriptor *desc = proc->descriptors->at(fd)->val;
        int64_t result = desc->lseek(offset, whence);
        proc->descriptors->at(fd)->mutex->unlock();
        return result;
    }
}; // namespace proc