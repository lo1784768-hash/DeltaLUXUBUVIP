#pragma once

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>

// Runtime value scan-and-patch, equivalent to h5gg's searchNumber/searchNearby/editAll.
// Ported to replace hard-coded function-pointer offsets, which break every game update;
// this instead finds the current address of a known value at runtime.
//
// Note: the iOS SDK blocks <mach/mach_vm.h> entirely ("mach_vm.h unsupported"), so this
// uses the un-prefixed legacy Mach vm_* calls (same family MemoryUtils.h's vm()/vm_unity()
// already use) instead of mach_vm_*. vm_address_t/vm_size_t are the native word size on
// arm64 (64-bit), so nothing is lost by not using the mach_vm_* 64-bit-explicit types.
class MemScanner {
public:
    void clearResults() { results.clear(); }
    size_t resultCount() const { return results.size(); }

    size_t searchNumber(const std::string &value, const std::string &type,
                         vm_address_t rangeStart = 0x100000000ULL,
                         vm_address_t rangeEnd = 0x200000000ULL)
    {
        results.clear();
        double lo, hi;
        parseRange(value, lo, hi);
        scanRange(rangeStart, rangeEnd, sizeOf(type), type, lo, hi, results);
        return results.size();
    }

    // Narrows the current result set to matches found within +/-window bytes of each address already found.
    size_t searchNearby(const std::string &value, const std::string &type, vm_size_t window)
    {
        double lo, hi;
        parseRange(value, lo, hi);
        size_t typeSize = sizeOf(type);

        std::vector<vm_address_t> narrowed;
        for (vm_address_t addr : results) {
            vm_address_t start = (addr > window) ? (addr - window) : 0;
            scanRange(start, addr + window, typeSize, type, lo, hi, narrowed);
        }
        results = narrowed;
        return results.size();
    }

    // Cheat-Engine-style "next scan": re-reads only the addresses already in the
    // result set (no new memory is scanned) and keeps the ones whose current value
    // now matches. Cheap, and what makes iterative search -> play -> search useful.
    size_t nextScan(const std::string &value, const std::string &type)
    {
        double lo, hi;
        parseRange(value, lo, hi);
        size_t typeSize = sizeOf(type);
        mach_port_t port = mach_task_self();

        std::vector<vm_address_t> narrowed;
        for (vm_address_t addr : results) {
            uint8_t buf[8];
            vm_size_t outSize = 0;
            if (vm_read_overwrite(port, addr, typeSize, (vm_address_t)buf, &outSize) == KERN_SUCCESS
                && outSize >= typeSize && matches(buf, type, lo, hi)) {
                narrowed.push_back(addr);
            }
        }
        results = narrowed;
        return results.size();
    }

    bool editAll(const std::string &value, const std::string &type)
    {
        if (results.empty()) return false;
        double lo, hi;
        parseRange(value, lo, hi);
        size_t typeSize = sizeOf(type);
        uint8_t buf[8];
        encode(lo, type, buf);

        mach_port_t port = mach_task_self();
        bool any = false;
        for (vm_address_t addr : results) {
            if (vm_protect(port, addr, typeSize, false, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS)
                continue;
            if (vm_write(port, addr, (vm_offset_t)buf, (mach_msg_type_number_t)typeSize) == KERN_SUCCESS)
                any = true;
            vm_protect(port, addr, typeSize, false, VM_PROT_READ | VM_PROT_EXECUTE);
        }
        return any;
    }

private:
    std::vector<vm_address_t> results;

    static size_t sizeOf(const std::string &type) {
        return (type == "I64") ? 8 : 4; // I32, F32 are both 4 bytes
    }

    // Supports either an exact value ("123") or a "min~max" range (used by Magic Bullet).
    static void parseRange(const std::string &value, double &lo, double &hi) {
        size_t tilde = value.find('~');
        if (tilde != std::string::npos) {
            lo = atof(value.substr(0, tilde).c_str());
            hi = atof(value.substr(tilde + 1).c_str());
        } else {
            lo = hi = atof(value.c_str());
        }
    }

    static void encode(double v, const std::string &type, uint8_t *out) {
        if (type == "I32") {
            int32_t iv = (int32_t)(int64_t)v;
            memcpy(out, &iv, 4);
        } else if (type == "I64") {
            int64_t iv = (int64_t)v;
            memcpy(out, &iv, 8);
        } else {
            float fv = (float)v;
            memcpy(out, &fv, 4);
        }
    }

    static bool matches(const uint8_t *bytes, const std::string &type, double lo, double hi) {
        double v;
        if (type == "I32") {
            int32_t iv; memcpy(&iv, bytes, 4); v = (double)iv;
        } else if (type == "I64") {
            int64_t iv; memcpy(&iv, bytes, 8); v = (double)iv;
        } else {
            float fv; memcpy(&fv, bytes, 4); v = (double)fv;
        }
        return v >= lo && v <= hi;
    }

    // Walks real mapped regions inside [start, end) via vm_region_64 and only reads
    // pages with VM_PROT_READ - never blind-reads the whole (mostly unmapped) span.
    static void scanRange(vm_address_t start, vm_address_t end, size_t typeSize,
                           const std::string &type, double lo, double hi,
                           std::vector<vm_address_t> &out)
    {
        mach_port_t port = mach_task_self();
        vm_address_t addr = start;

        while (addr < end) {
            vm_address_t regionAddr = addr;
            vm_size_t regionSize = 0;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t objName;

            kern_return_t kr = vm_region_64(port, &regionAddr, &regionSize,
                                             VM_REGION_BASIC_INFO_64,
                                             (vm_region_info_t)&info, &infoCount, &objName);
            if (kr != KERN_SUCCESS) break;
            if (regionAddr >= end) break;

            if (info.protection & VM_PROT_READ) {
                const vm_size_t chunkSize = 1024 * 1024;
                vm_address_t curr = regionAddr;
                vm_address_t regionEnd = regionAddr + regionSize;
                if (regionEnd > end) regionEnd = end;

                std::vector<uint8_t> buffer;
                while (curr < regionEnd) {
                    vm_size_t remaining = regionEnd - curr;
                    vm_size_t readSize = (remaining < chunkSize) ? remaining : chunkSize;
                    buffer.resize((size_t)readSize);
                    vm_size_t outSize = 0;
                    kern_return_t rkr = vm_read_overwrite(port, curr, readSize,
                                                           (vm_address_t)buffer.data(), &outSize);
                    if (rkr == KERN_SUCCESS) {
                        for (vm_size_t i = 0; i + typeSize <= outSize; i += typeSize) {
                            if (matches(buffer.data() + i, type, lo, hi))
                                out.push_back(curr + i);
                        }
                    }
                    curr += readSize;
                }
            }

            addr = regionAddr + regionSize;
        }
    }
};
