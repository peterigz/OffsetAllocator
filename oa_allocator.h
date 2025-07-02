#ifndef OA_ALLOCATOR_INCLUDE_H
#define OA_ALLOCATOR_INCLUDE_H

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#ifdef DEBUG
#include <assert.h>
#define oa_ASSERT(x) assert(x)
//#define DEBUG_VERBOSE
#else
#define oa_ASSERT(x)
#endif

#ifndef OA_API
#define OA_API
#endif

#define oa__KILOBYTE(Value) ((Value) * 1024LL)
#define oa__MEGABYTE(Value) (oa__KILOBYTE(Value) * 1024LL)
#define oa__GIGABYTE(Value) (oa__MEGABYTE(Value) * 1024LL)

typedef int oa_index;
typedef unsigned char oa_u8;
typedef unsigned short oa_u16;
typedef unsigned int oa_sl_bitmap;
typedef unsigned int oa_uint;
typedef size_t oa_size;
typedef unsigned int oa_gpu_node_index;
typedef int oa_bool;
typedef void *oa_pool;

#ifdef __cplusplus
extern "C" {
#endif

#if defined (_MSC_VER) && (_MSC_VER >= 1400) && (defined (_M_IX86) || defined (_M_X64))
    /* Microsoft Visual C++ support on x86/X64 architectures. */

#include <intrin.h>

    static inline int oa__scan_reverse(oa_uint bitmap) {
        unsigned long index;
        return _BitScanReverse(&index, bitmap) ? index : -1;
    }

    static inline int oa__scan_forward(oa_uint bitmap)
    {
        unsigned long index;
        return _BitScanForward(&index, bitmap) ? index : -1;
    }

#elif defined(__GNUC__) && ((__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)) && \
      (defined(__i386__) || defined(__x86_64__)) || defined(__clang__)
    /* GNU C/C++ or Clang support on x86/x64 architectures. */

    static inline int oa__scan_reverse(oa_uint bitmap)
    {
        return 32 - __builtin_clz((int)bitmap) - 1;
    }

    static inline int oa__scan_forward(oa_uint bitmap)
    {
        return __builtin_ffs((int)bitmap) - 1;
    }

#endif

    enum oa__constants {
        oa__NUM_TOP_BINS = 32,
        oa__BINS_PER_LEAF = 8,
        oa__TOP_BINS_INDEX_SHIFT = 3,
        oa__LEAF_BINS_INDEX_MASK = 0x7,
        oa__NUM_LEAF_BINS = oa__NUM_TOP_BINS * oa__BINS_PER_LEAF,
        oa__UNUSED = 0xffffffff,
        oa__NO_SPACE = 0xffffffff,
        oa__MANTISSA_BITS = 3,
        oa__MANTISSA_VALUE = 1 << oa__MANTISSA_BITS,
        oa__MANTISSA_MASK = oa__MANTISSA_VALUE - 1,
    };

    typedef struct oa_allocation_t {
        oa_uint offset;
        oa_gpu_node_index metadata; // internal: node index
    } oa_allocation_t;

    typedef struct oa_storage_report_t {
        oa_uint totalFreeSpace;
        oa_uint largestFreeRegion;
    } oa_storage_report_t;

    typedef struct oa_region_t {
        oa_uint size;
        oa_uint count;
    } oa_region_t;

    typedef struct oa_storage_report_full_t {
        oa_region_t freeRegions[oa__NUM_LEAF_BINS];
    } oa_storage_report_full_t;

    typedef struct oa_node_t {
        oa_uint dataOffset;
        oa_uint dataSize;
        oa_gpu_node_index binListPrev;
        oa_gpu_node_index binListNext;
        oa_gpu_node_index neighbourPrev;
        oa_gpu_node_index neighbourNext;
        int used; // TODO: Merge as bit flag
    } oa_node_t;

    typedef struct oa_allocator_t {
        oa_uint m_size;
        oa_uint m_maxAllocs;
        oa_uint m_freeStorage;

        oa_uint m_usedBinsTop;
        oa_u8 m_usedBins[oa__NUM_TOP_BINS];
        oa_gpu_node_index m_binIndices[oa__NUM_LEAF_BINS];

        oa_node_t *m_nodes;
        oa_gpu_node_index *m_freeNodes;
        oa_uint m_freeOffset;
    } oa_allocator_t;

    // Bin sizes follow floating point (exponent + mantissa) distribution (piecewise linear log approx)
    // This ensures that for each size class, the average overhead percentage stays the same
    inline oa_uint oa__uint_to_float_round_up(oa_uint size)
    {
        oa_uint exp = 0;
        oa_uint mantissa = 0;

        if (size < oa__MANTISSA_VALUE)
        {
            // Denorm: 0..(oa__MANTISSA_VALUE-1)
            mantissa = size;
        } else
        {
            // Normalized: Hidden high bit always 1. Not stored. Just like float.
            oa_uint leadingZeros = oa__scan_reverse(size);
            oa_uint highestSetBit = leadingZeros;

            oa_uint mantissaStartBit = highestSetBit - oa__MANTISSA_BITS;
            exp = mantissaStartBit + 1;
            mantissa = (size >> mantissaStartBit) & oa__MANTISSA_MASK;

            oa_uint lowBitsMask = (1 << mantissaStartBit) - 1;

            // Round up!
            if ((size & lowBitsMask) != 0)
                mantissa++;
        }

        return (exp << oa__MANTISSA_BITS) + mantissa; // + allows mantissa->exp overflow for round up
    }

    inline oa_uint oa__uint_to_float_round_down(oa_uint size)
    {
        oa_uint exp = 0;
        oa_uint mantissa = 0;

        if (size < oa__MANTISSA_VALUE)
        {
            // Denorm: 0..(oa__MANTISSA_VALUE-1)
            mantissa = size;
        } else
        {
            // Normalized: Hidden high bit always 1. Not stored. Just like float.
            oa_uint leadingZeros = oa__scan_reverse(size);
            oa_uint highestSetBit = leadingZeros;

            oa_uint mantissaStartBit = highestSetBit - oa__MANTISSA_BITS;
            exp = mantissaStartBit + 1;
            mantissa = (size >> mantissaStartBit) & oa__MANTISSA_MASK;
        }

        return (exp << oa__MANTISSA_BITS) | mantissa;
    }

    inline oa_uint oa__float_to_uint(oa_uint floatValue)
    {
        oa_uint exponent = floatValue >> oa__MANTISSA_BITS;
        oa_uint mantissa = floatValue & oa__MANTISSA_MASK;
        if (exponent == 0)
        {
            // Denorms
            return mantissa;
        } else
        {
            return (mantissa | oa__MANTISSA_VALUE) << (exponent - 1);
        }
    }

    // Utility functions
    inline oa_uint oa__find_lowest_set_bit_after(oa_uint bitMask, oa_uint startBitIndex)
    {
        oa_uint maskBeforeStartIndex = (1 << startBitIndex) - 1;
        oa_uint maskAfterStartIndex = ~maskBeforeStartIndex;
        oa_uint bitsAfter = bitMask & maskAfterStartIndex;
        if (bitsAfter == 0) return oa__NO_SPACE;
        return oa__scan_forward(bitsAfter);
    }

    inline oa_uint oa__insert_node_into_bin(oa_allocator_t *gpu_allocator, oa_uint size, oa_uint dataOffset)
    {
        // Round down to bin index to ensure that bin >= alloc
        oa_uint binIndex = oa__uint_to_float_round_down(size);

        oa_uint topBinIndex = binIndex >> oa__TOP_BINS_INDEX_SHIFT;
        oa_uint leafBinIndex = binIndex & oa__LEAF_BINS_INDEX_MASK;

        // Bin was empty before?
        if (gpu_allocator->m_binIndices[binIndex] == oa__UNUSED)
        {
            // Set bin mask bits
            gpu_allocator->m_usedBins[topBinIndex] |= 1 << leafBinIndex;
            gpu_allocator->m_usedBinsTop |= 1 << topBinIndex;
        }

        // Take a freelist node and insert on top of the bin linked list (next = old top)
        oa_uint topNodeIndex = gpu_allocator->m_binIndices[binIndex];
        oa_uint nodeIndex = gpu_allocator->m_freeNodes[gpu_allocator->m_freeOffset--];
        gpu_allocator->m_nodes[nodeIndex] = (oa_node_t) {
            .dataOffset = dataOffset,
            .dataSize = size,
            .binListPrev = oa__UNUSED,
            .binListNext = topNodeIndex,
            .neighbourPrev = oa__UNUSED,
            .neighbourNext = oa__UNUSED,
            .used = 0
        };
        if (topNodeIndex != oa__UNUSED) gpu_allocator->m_nodes[topNodeIndex].binListPrev = nodeIndex;
        gpu_allocator->m_binIndices[binIndex] = nodeIndex;

        gpu_allocator->m_freeStorage += size;

        return nodeIndex;
    }

    inline void oa__gpu_allocator_reset(oa_allocator_t *gpu_allocator)
    {
        gpu_allocator->m_freeStorage = 0;
        gpu_allocator->m_usedBinsTop = 0;
        gpu_allocator->m_freeOffset = gpu_allocator->m_maxAllocs;

        for (oa_uint i = 0; i < oa__NUM_TOP_BINS; i++)
            gpu_allocator->m_usedBins[i] = 0;

        for (oa_uint i = 0; i < oa__NUM_LEAF_BINS; i++)
            gpu_allocator->m_binIndices[i] = oa__UNUSED;

        oa_size node_memory_size = sizeof(oa_node_t) * (gpu_allocator->m_maxAllocs + 1);
        memset(gpu_allocator->m_nodes, 0, node_memory_size);

        // Freelist is a stack. Nodes in inverse order so that [0] pops first.
        for (oa_uint i = 0; i < gpu_allocator->m_maxAllocs + 1; i++)
        {
            gpu_allocator->m_freeNodes[i] = gpu_allocator->m_maxAllocs - i;
        }

        // Start state: Whole storage as one big node
        // Algorithm will split remainders and push them back as smaller nodes
        oa__insert_node_into_bin(gpu_allocator, gpu_allocator->m_size, 0);
    }

    inline void oa__remove_node_from_bin(oa_allocator_t *gpu_allocator, oa_uint nodeIndex)
    {
        oa_node_t *node = &gpu_allocator->m_nodes[nodeIndex];

        if (node->binListPrev != oa__UNUSED)
        {
            // Easy case: We have previous node. Just remove this node from the middle of the list.
            gpu_allocator->m_nodes[node->binListPrev].binListNext = node->binListNext;
            if (node->binListNext != oa__UNUSED) gpu_allocator->m_nodes[node->binListNext].binListPrev = node->binListPrev;
        } else
        {
            // Hard case: We are the first node in a bin. Find the bin.

            // Round down to bin index to ensure that bin >= alloc
            oa_uint binIndex = oa__uint_to_float_round_down(node->dataSize);

            oa_uint topBinIndex = binIndex >> oa__TOP_BINS_INDEX_SHIFT;
            oa_uint leafBinIndex = binIndex & oa__LEAF_BINS_INDEX_MASK;

            gpu_allocator->m_binIndices[binIndex] = node->binListNext;
            if (node->binListNext != oa__UNUSED) gpu_allocator->m_nodes[node->binListNext].binListPrev = oa__UNUSED;

            // Bin empty?
            if (gpu_allocator->m_binIndices[binIndex] == oa__UNUSED)
            {
                // Remove a leaf bin mask bit
                gpu_allocator->m_usedBins[topBinIndex] &= ~(1 << leafBinIndex);

                // All leaf bins empty?
                if (gpu_allocator->m_usedBins[topBinIndex] == 0)
                {
                    // Remove a top bin mask bit
                    gpu_allocator->m_usedBinsTop &= ~(1 << topBinIndex);
                }
            }
        }

        // Insert the node to freelist
        gpu_allocator->m_freeNodes[++gpu_allocator->m_freeOffset] = nodeIndex;

        gpu_allocator->m_freeStorage -= node->dataSize;
    }

    OA_API oa_size oa_CalculateAllocatorSize(oa_uint max_allocations);
    OA_API oa_uint oa_AllocationSize(oa_allocator_t *gpu_allocator, oa_allocation_t allocation);
    OA_API oa_storage_report_t oa_StorageReport(oa_allocator_t *gpu_allocator);
    OA_API oa_storage_report_full_t oa_StorageReportFull(oa_allocator_t *gpu_allocator);
    OA_API oa_allocator_t *oa_CreateAllocator(void *memory, oa_uint size, oa_uint max_allocations);
    OA_API oa_allocation_t oa_Allocate(oa_allocator_t *gpu_allocator, oa_uint size);
    OA_API void oa_Free(oa_allocator_t *gpu_allocator, oa_allocation_t allocation);

#ifdef __cplusplus
}
#endif

#if defined(OA_IMPLEMENTATION)

oa_size oa_CalculateAllocatorSize(oa_uint max_allocations) {
    oa_size node_memory_size = sizeof(oa_node_t) * (max_allocations + 1);
    oa_size free_node_memory_size = sizeof(oa_gpu_node_index) * (max_allocations + 1);
    return sizeof(oa_allocator_t) + node_memory_size + free_node_memory_size;
}

oa_allocator_t *oa_CreateAllocator(void *memory, oa_uint size, oa_uint max_allocations) {
    oa_allocator_t *gpu_allocator = (oa_allocator_t *)memory;
    *gpu_allocator = (oa_allocator_t){ 0 };
    gpu_allocator->m_size = size;
    gpu_allocator->m_maxAllocs = max_allocations;
    if (sizeof(oa_gpu_node_index) == 2)
    {
        oa_ASSERT(max_allocations <= 65536);
    }
    oa_size node_memory_size = sizeof(oa_node_t) * (gpu_allocator->m_maxAllocs + 1);
    char *mem_offset = (char *)memory + sizeof(oa_allocator_t);
    gpu_allocator->m_nodes = (oa_node_t *)mem_offset;
    mem_offset += node_memory_size;
    gpu_allocator->m_freeNodes = (oa_gpu_node_index *)mem_offset;
    oa__gpu_allocator_reset(gpu_allocator);
    return gpu_allocator;
}

oa_allocation_t oa_Allocate(oa_allocator_t *gpu_allocator, oa_uint size) {
    // Out of allocations?
    if (gpu_allocator->m_freeOffset == oa__NO_SPACE)
    {
        return (oa_allocation_t) { .offset = oa__NO_SPACE, .metadata = oa__NO_SPACE };
    }

    // Round up to bin index to ensure that alloc >= bin
    // Gives us min bin index that fits the size
    oa_uint minBinIndex = oa__uint_to_float_round_up(size);

    oa_uint minTopBinIndex = minBinIndex >> oa__TOP_BINS_INDEX_SHIFT;
    oa_uint minLeafBinIndex = minBinIndex & oa__LEAF_BINS_INDEX_MASK;

    oa_uint topBinIndex = minTopBinIndex;
    oa_uint leafBinIndex = oa__NO_SPACE;

    // If top bin exists, scan its leaf bin. This can fail (NO_SPACE).
    if (gpu_allocator->m_usedBinsTop & (1 << topBinIndex))
    {
        leafBinIndex = oa__find_lowest_set_bit_after(gpu_allocator->m_usedBins[topBinIndex], minLeafBinIndex);
    }

    // If we didn't find space in top bin, we search top bin from +1
    if (leafBinIndex == oa__NO_SPACE)
    {
        topBinIndex = oa__find_lowest_set_bit_after(gpu_allocator->m_usedBinsTop, minTopBinIndex + 1);

        // Out of space?
        if (topBinIndex == oa__NO_SPACE)
        {
            return (oa_allocation_t) { .offset = oa__NO_SPACE, .metadata = oa__NO_SPACE };
        }

        // All leaf bins here fit the alloc, since the top bin was rounded up. Start leaf search from bit 0.
        // NOTE: This search can't fail since at least one leaf bit was set because the top bit was set.
        leafBinIndex = oa__scan_forward(gpu_allocator->m_usedBins[topBinIndex]);
    }

    oa_uint binIndex = (topBinIndex << oa__TOP_BINS_INDEX_SHIFT) | leafBinIndex;

    // Pop the top node of the bin. Bin top = node.next.
    oa_uint nodeIndex = gpu_allocator->m_binIndices[binIndex];
    oa_ASSERT(nodeIndex < gpu_allocator->m_maxAllocs);
    oa_node_t *node = &gpu_allocator->m_nodes[nodeIndex];
    oa_uint nodeTotalSize = node->dataSize;
    node->dataSize = size;
    node->used = 1;
    gpu_allocator->m_binIndices[binIndex] = node->binListNext;
    if (node->binListNext != oa__UNUSED) gpu_allocator->m_nodes[node->binListNext].binListPrev = oa__UNUSED;
    gpu_allocator->m_freeStorage -= nodeTotalSize;

    // Bin empty?
    if (gpu_allocator->m_binIndices[binIndex] == oa__UNUSED)
    {
        // Remove a leaf bin mask bit
        gpu_allocator->m_usedBins[topBinIndex] &= ~(1 << leafBinIndex);

        // All leaf bins empty?
        if (gpu_allocator->m_usedBins[topBinIndex] == 0)
        {
            // Remove a top bin mask bit
            gpu_allocator->m_usedBinsTop &= ~(1 << topBinIndex);
        }
    }

    // Push back reminder N elements to a lower bin
    oa_uint reminderSize = nodeTotalSize - size;
    if (reminderSize > 0)
    {
        oa_uint newNodeIndex = oa__insert_node_into_bin(gpu_allocator, reminderSize, node->dataOffset + size);

        // Link nodes next to each other so that we can merge them later if both are free
        // And update the old next neighbor to point to the new node (in middle)
        if (node->neighbourNext != oa__UNUSED) gpu_allocator->m_nodes[node->neighbourNext].neighbourPrev = newNodeIndex;
        gpu_allocator->m_nodes[newNodeIndex].neighbourPrev = nodeIndex;
        gpu_allocator->m_nodes[newNodeIndex].neighbourNext = node->neighbourNext;
        node->neighbourNext = newNodeIndex;
    }

    return (oa_allocation_t) { .offset = node->dataOffset, .metadata = nodeIndex };
}

void oa_Free(oa_allocator_t *gpu_allocator, oa_allocation_t allocation) {
    oa_ASSERT(allocation.metadata != oa__NO_SPACE);
    if (!gpu_allocator->m_nodes) return;

    oa_uint nodeIndex = allocation.metadata;
    oa_node_t *node = &gpu_allocator->m_nodes[nodeIndex];

    // Double delete check
    oa_ASSERT(node->used);

    // Merge with neighbors...
    oa_uint offset = node->dataOffset;
    oa_uint size = node->dataSize;

    if ((node->neighbourPrev != oa__UNUSED) && (gpu_allocator->m_nodes[node->neighbourPrev].used == 0))
    {
        // Previous (contiguous) free node-> Sum sizes
        oa_node_t *prevNode = &gpu_allocator->m_nodes[node->neighbourPrev];
        offset = prevNode->dataOffset;
        size += prevNode->dataSize;

        // Remove node from the bin linked list and put it in the freelist
        oa__remove_node_from_bin(gpu_allocator, node->neighbourPrev);

        oa_ASSERT(prevNode->neighbourNext == nodeIndex);
        node->neighbourPrev = prevNode->neighbourPrev;
    }

    if ((node->neighbourNext != oa__UNUSED) && (gpu_allocator->m_nodes[node->neighbourNext].used == 0))
    {
        // Next (contiguous) free node-> Sum sizes.
        oa_node_t *nextNode = &gpu_allocator->m_nodes[node->neighbourNext];
        size += nextNode->dataSize;

        // Remove node from the bin linked list and put it in the freelist
        oa__remove_node_from_bin(gpu_allocator, node->neighbourNext);

        oa_ASSERT(nextNode->neighbourPrev == nodeIndex);
        node->neighbourNext = nextNode->neighbourNext;
    }

    oa_uint neighbourNext = node->neighbourNext;
    oa_uint neighbourPrev = node->neighbourPrev;

    // Insert the removed node to freelist
    gpu_allocator->m_freeNodes[++gpu_allocator->m_freeOffset] = nodeIndex;

    // Insert the (combined) free node to bin
    oa_uint combinedNodeIndex = oa__insert_node_into_bin(gpu_allocator, size, offset);

    // Connect neighbors with the new combined node
    if (neighbourNext != oa__UNUSED)
    {
        gpu_allocator->m_nodes[combinedNodeIndex].neighbourNext = neighbourNext;
        gpu_allocator->m_nodes[neighbourNext].neighbourPrev = combinedNodeIndex;
    }
    if (neighbourPrev != oa__UNUSED)
    {
        gpu_allocator->m_nodes[combinedNodeIndex].neighbourPrev = neighbourPrev;
        gpu_allocator->m_nodes[neighbourPrev].neighbourNext = combinedNodeIndex;
    }
}

oa_storage_report_t oa_StorageReport(oa_allocator_t *gpu_allocator) {
    oa_uint largestFreeRegion = 0;
    oa_uint freeStorage = 0;

    // Out of allocations? -> Zero free space
    if (gpu_allocator->m_freeOffset > 0)
    {
        freeStorage = gpu_allocator->m_freeStorage;
        if (gpu_allocator->m_usedBinsTop)
        {
            oa_uint topBinIndex = oa__scan_reverse(gpu_allocator->m_usedBinsTop);
            oa_uint leafBinIndex = oa__scan_reverse(gpu_allocator->m_usedBins[topBinIndex]);
            largestFreeRegion = oa__float_to_uint((topBinIndex << oa__TOP_BINS_INDEX_SHIFT) | leafBinIndex);
            oa_ASSERT(freeStorage >= largestFreeRegion);
        }
    }

    return (oa_storage_report_t) { .totalFreeSpace = freeStorage, .largestFreeRegion = largestFreeRegion };
}

oa_storage_report_full_t oa_StorageReportFull(oa_allocator_t *gpu_allocator) {
    oa_storage_report_full_t report;
    for (oa_uint i = 0; i < oa__NUM_LEAF_BINS; i++) {
        oa_uint count = 0;
        oa_uint nodeIndex = gpu_allocator->m_binIndices[i];
        while (nodeIndex != oa__UNUSED) {
            nodeIndex = gpu_allocator->m_nodes[nodeIndex].binListNext;
            count++;
        }
        report.freeRegions[i] = (oa_region_t){ .size = oa__float_to_uint(i), .count = count };
    }
    return report;
}

oa_uint oa_AllocationSize(oa_allocator_t *gpu_allocator, oa_allocation_t allocation) {
    if (allocation.metadata == oa__NO_SPACE) return 0;
    if (!gpu_allocator->m_nodes) return 0;
    return gpu_allocator->m_nodes[allocation.metadata].dataSize;
}

#endif

#endif
