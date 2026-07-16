#pragma once

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdlib>

// Runtime value scan-and-patch, equivalent to h5gg's searchNumber/searchNearby/editAll.
// Ported to replace hard-coded function-pointer offsets, which break every game update;
// this instead finds the current address of a known value at runtime.
class MemScanner {
public:
    void clearResults() { results.clear(); }
    size_t resultCount() const { return results.size(); }

    size_t searchNumber(const std::string &value, const std::string &type,
                         mach_vm_address_t rangeStart = 0x100000000ULL,
                         mach_vm_address_t rangeEnd = 0x200000000ULL)
    {
        results.clear();
        double lo, hi;
        parseRange(value, lo, hi);
        scanRange(rangeStart, rangeEnd, sizeOf(type), type, lo, hi, results);
        return results.size();
    }

    // Narrows the current result set to matches found within +/-window bytes of each address already found.
    size_t searchNearby(const std::string &value, const std::string &type, mach_vm_size_t window)
    {
        double lo, hi;
        parseRange(value, lo, hi);
        size_t typeSize = sizeOf(type);

        std::vector<mach_vm_address_t> narrowed;
        for (mach_vm_address_t addr : results) {
            mach_vm_address_t start = (addr > window) ? (addr - window) : 0;
            scanRange(start, addr + window, typeSize, type, lo, hi, narrowed);
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
        for (mach_vm_address_t addr : results) {
            if (mach_vm_protect(port, addr, typeSize, false, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS)
                continue;
            if (mach_vm_write(port, addr, (vm_offset_t)buf, (mach_msg_type_number_t)typeSize) == KERN_SUCCESS)
                any = true;
            mach_vm_protect(port, addr, typeSize, false, VM_PROT_READ | VM_PROT_EXECUTE);
        }
        return any;
    }

private:
    std::vector<mach_vm_address_t> results;

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

    // Walks real mapped regions inside [start, end) via mach_vm_region and only reads
    // pages with VM_PROT_READ - never blind-reads the whole (mostly unmapped) span.
    static void scanRange(mach_vm_address_t start, mach_vm_address_t end, size_t typeSize,
                           const std::string &type, double lo, double hi,
                           std::vector<mach_vm_address_t> &out)
    {
        mach_port_t port = mach_task_self();
        mach_vm_address_t addr = start;

        while (addr < end) {
            mach_vm_address_t regionAddr = addr;
            mach_vm_size_t regionSize = 0;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t objName;

            kern_return_t kr = mach_vm_region(port, &regionAddr, &regionSize,
                                               VM_REGION_BASIC_INFO_64,
                                               (vm_region_info_t)&info, &infoCount, &objName);
            if (kr != KERN_SUCCESS) break;
            if (regionAddr >= end) break;

            if (info.protection & VM_PROT_READ) {
                const mach_vm_size_t chunkSize = 1024 * 1024;
                mach_vm_address_t curr = regionAddr;
                mach_vm_address_t regionEnd = regionAddr + regionSize;
                if (regionEnd > end) regionEnd = end;

                std::vector<uint8_t> buffer;
                while (curr < regionEnd) {
                    mach_vm_size_t remaining = regionEnd - curr;
                    mach_vm_size_t readSize = (remaining < chunkSize) ? remaining : chunkSize;
                    buffer.resize((size_t)readSize);
                    mach_vm_size_t outSize = 0;
                    kern_return_t rkr = mach_vm_read_overwrite(port, curr, readSize,
                                                                (mach_vm_address_t)buffer.data(), &outSize);
                    if (rkr == KERN_SUCCESS) {
                        for (mach_vm_size_t i = 0; i + typeSize <= outSize; i += typeSize) {
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
