#ifndef __TSS_HPP_INCLUDED__
#define __TSS_HPP_INCLUDED__

#include <utils.hpp>

namespace x86_64 {
#pragma pack(1)
    struct TSSLayout {
        uint32_t : 32;
        uint64_t rsp[3];
        uint64_t : 64;
        uint64_t ist[7];
        uint64_t : 64;
        uint16_t : 16;
        uint16_t iopb;
    };
#pragma pack(0)
    class TSS {
        static TSSLayout tss;

    public:
        static uint64_t getBase();
        static void setKernelStack(uint64_t rsp);
        static void init();
    };

}; // namespace x86_64

#endif