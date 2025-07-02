#define OA_IMPLEMENTATION
#include "oa_allocator.h"
#include <stdio.h>
#include <stdlib.h>

#define KGRN  "\x1B[32m"
#define KRED  "\x1B[31m"
#define KNRM  "\x1B[0m"

int g_pass_count = 0;
int g_fail_count = 0;

#define CHECK(condition, name) \
    if (condition) { \
        g_pass_count++; \
        printf("%sPASS: %s%s\n", KGRN, name, KNRM); \
    } else { \
        g_fail_count++; \
        printf("%sFAIL: %s%s\n", KRED, name, KNRM); \
    }

// We want to allocate the shortest lifetime memory last and the longest
// lifetime first.

int main(int argc, char **argv) {
    // Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
    // NOTE: Assuming 8 value (3 bit) mantissa.
    // If this test fails, please change this assumption!
    {
        oa_uint preciseNumberCount = 17;
        for (oa_uint i = 0; i < preciseNumberCount; i++)
        {
            oa_uint roundUp = oa__uint_to_float_round_up(i);
            oa_uint roundDown = oa__uint_to_float_round_down(i);
            CHECK(i == roundUp, "uint_to_float_round_up precise");
            CHECK(i == roundDown, "uint_to_float_round_down precise");
        }

        // Test some random picked numbers
        typedef struct NumberFloatUpDown
        {
            oa_uint number;
            oa_uint up;
            oa_uint down;
        }NumberFloatUpDown;

        NumberFloatUpDown testData[] = {
            {.number = 17, .up = 17, .down = 16},
            {.number = 118, .up = 39, .down = 38},
            {.number = 1024, .up = 64, .down = 64},
            {.number = 65536, .up = 112, .down = 112},
            {.number = 529445, .up = 137, .down = 136},
            {.number = 1048575, .up = 144, .down = 143},
        };

        for (oa_uint i = 0; i < sizeof(testData) / sizeof(NumberFloatUpDown); i++)
        {
            NumberFloatUpDown v = testData[i];
            oa_uint roundUp = oa__uint_to_float_round_up(v.number);
            oa_uint roundDown = oa__uint_to_float_round_down(v.number);
            CHECK(roundUp == v.up, "uint_to_float_round_up random");
            CHECK(roundDown == v.down, "uint_to_float_round_down random");
        }
    }

    {
        // Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
        // NOTE: Assuming 8 value (3 bit) mantissa.
        // If this test fails, please change this assumption!
        oa_uint preciseNumberCount = 17;
        for (oa_uint i = 0; i < preciseNumberCount; i++)
        {
            oa_uint v = oa__float_to_uint(i);
            CHECK(i == v, "float_to_uint precise");
        }

        // Test that float->uint->float conversion is precise for all numbers
        // NOTE: Test values < 240. 240->4G = overflows 32 bit integer
        for (oa_uint i = 0; i < 240; i++)
        {
            oa_uint v = oa__float_to_uint(i);
            oa_uint roundUp = oa__uint_to_float_round_up(v);
            oa_uint roundDown = oa__uint_to_float_round_down(v);
            CHECK(i == roundUp, "float_to_uint_to_float round up precise");
            CHECK(i == roundDown, "float_to_uint_to_float round down precise");
        }
    }

    {
        void *memory = malloc(oa_CalculateAllocatorSize(100));
        oa_allocator_t *gpu_allocator = oa_CreateAllocator(memory, oa__MEGABYTE(2), 100);
        oa_allocation_t a = oa_Allocate(gpu_allocator, 1337);
        oa_uint offset = a.offset;
        CHECK(offset == 0, "Simple allocation offset");
        oa_Free(gpu_allocator, a);
        free(gpu_allocator);
    }

    {
        void *memory = malloc(oa_CalculateAllocatorSize(100));
        oa_allocator_t *gpu_allocator = oa_CreateAllocator(memory, oa__MEGABYTE(256), 100);
        oa_allocation_t a = oa_Allocate(gpu_allocator, 0);
        CHECK(a.offset == 0, "Allocation a offset");

        oa_allocation_t b = oa_Allocate(gpu_allocator, 1);
        CHECK(b.offset == 0, "Allocation b offset");

        oa_allocation_t c = oa_Allocate(gpu_allocator, 123);
        CHECK(c.offset == 1, "Allocation c offset");

        oa_allocation_t d = oa_Allocate(gpu_allocator, 1234);
        CHECK(d.offset == 124, "Allocation d offset");

        oa_Free(gpu_allocator, a);
        oa_Free(gpu_allocator, b);
        oa_Free(gpu_allocator, c);
        oa_Free(gpu_allocator, d);

        oa_allocation_t validateAll = oa_Allocate(gpu_allocator, oa__MEGABYTE(256));
        CHECK(validateAll.offset == 0, "No fragmentation after free");
        oa_Free(gpu_allocator, validateAll);

        free(gpu_allocator);
    }

    {
        void *memory = malloc(oa_CalculateAllocatorSize(100));
        oa_allocator_t *gpu_allocator = oa_CreateAllocator(memory, oa__MEGABYTE(256), 100);
        oa_allocation_t a = oa_Allocate(gpu_allocator, 1337);
        CHECK(a.offset == 0, "Reuse after free offset");
        oa_Free(gpu_allocator, a);

        oa_allocation_t b = oa_Allocate(gpu_allocator, 1337);
        CHECK(b.offset == 0, "Reuse after free offset 2");
        oa_Free(gpu_allocator, b);

        oa_allocation_t validateAll = oa_Allocate(gpu_allocator, oa__MEGABYTE(256));
        CHECK(validateAll.offset == 0, "No fragmentation after reuse");
        oa_Free(gpu_allocator, validateAll);

        free(gpu_allocator);
    }

    {
        void *memory = malloc(oa_CalculateAllocatorSize(100));
        oa_allocator_t *gpu_allocator = oa_CreateAllocator(memory, oa__MEGABYTE(256), 100);
        oa_allocation_t a = oa_Allocate(gpu_allocator, 1024);
        CHECK(a.offset == 0, "Reuse from bin a");

        oa_allocation_t b = oa_Allocate(gpu_allocator, 3456);
        CHECK(b.offset == 1024, "Reuse from bin b");

        oa_Free(gpu_allocator, a);

        oa_allocation_t c = oa_Allocate(gpu_allocator, 1024);
        CHECK(c.offset == 0, "Reuse from bin c");

        oa_Free(gpu_allocator, c);
        oa_Free(gpu_allocator, b);

        oa_allocation_t validateAll = oa_Allocate(gpu_allocator, oa__MEGABYTE(256));
        CHECK(validateAll.offset == 0, "No fragmentation after bin reuse");
        oa_Free(gpu_allocator, validateAll);
        free(gpu_allocator);
    }

    {
        void *memory = malloc(oa_CalculateAllocatorSize(100));
        oa_allocator_t *gpu_allocator = oa_CreateAllocator(memory, oa__MEGABYTE(256), 100);
        oa_allocation_t a = oa_Allocate(gpu_allocator, 1024);
        CHECK(a.offset == 0, "No reuse from bin a");

        oa_allocation_t b = oa_Allocate(gpu_allocator, 3456);
        CHECK(b.offset == 1024, "No reuse from bin b");

        oa_Free(gpu_allocator, a);

        oa_allocation_t c = oa_Allocate(gpu_allocator, 2345);
        CHECK(c.offset == 1024 + 3456, "No reuse from bin c");

        oa_allocation_t d = oa_Allocate(gpu_allocator, 456);
        CHECK(d.offset == 0, "No reuse from bin d");

        oa_allocation_t e = oa_Allocate(gpu_allocator, 512);
        CHECK(e.offset == 456, "No reuse from bin e");

        oa_storage_report_t report = oa_StorageReport(gpu_allocator);
        CHECK(report.totalFreeSpace == oa__MEGABYTE(256) - 3456 - 2345 - 456 - 512, "Storage report totalFreeSpace");
        CHECK(report.largestFreeRegion != report.totalFreeSpace, "Storage report largestFreeRegion");

        oa_Free(gpu_allocator, c);
        oa_Free(gpu_allocator, d);
        oa_Free(gpu_allocator, b);
        oa_Free(gpu_allocator, e);

        oa_allocation_t validateAll = oa_Allocate(gpu_allocator, oa__MEGABYTE(256));
        CHECK(validateAll.offset == 0, "No fragmentation after no bin reuse");
        oa_Free(gpu_allocator, validateAll);
        free(gpu_allocator);
    }

    {
        void *memory = malloc(oa_CalculateAllocatorSize(256));
        oa_allocator_t *gpu_allocator = oa_CreateAllocator(memory, oa__MEGABYTE(256), 256);
        oa_allocation_t allocations[256];
        for (oa_uint i = 0; i < 256; i++)
        {
            allocations[i] = oa_Allocate(gpu_allocator, 1024 * 1024);
            CHECK(allocations[i].offset == i * 1024 * 1024, "Bulk allocation");
        }

        oa_storage_report_t report = oa_StorageReport(gpu_allocator);
        CHECK(report.totalFreeSpace == 0, "Storage report after bulk allocation totalFreeSpace");
        CHECK(report.largestFreeRegion == 0, "Storage report after bulk allocation largestFreeRegion");

        oa_Free(gpu_allocator, allocations[243]);
        oa_Free(gpu_allocator, allocations[5]);
        oa_Free(gpu_allocator, allocations[123]);
        oa_Free(gpu_allocator, allocations[95]);

        oa_Free(gpu_allocator, allocations[151]);
        oa_Free(gpu_allocator, allocations[152]);
        oa_Free(gpu_allocator, allocations[153]);
        oa_Free(gpu_allocator, allocations[154]);

        allocations[243] = oa_Allocate(gpu_allocator, 1024 * 1024);
        allocations[5] = oa_Allocate(gpu_allocator, 1024 * 1024);
        allocations[123] = oa_Allocate(gpu_allocator, 1024 * 1024);
        allocations[95] = oa_Allocate(gpu_allocator, 1024 * 1024);
        allocations[151] = oa_Allocate(gpu_allocator, 1024 * 1024 * 4);
        CHECK(allocations[243].offset != oa__NO_SPACE, "Reallocation 1");
        CHECK(allocations[5].offset != oa__NO_SPACE, "Reallocation 2");
        CHECK(allocations[123].offset != oa__NO_SPACE, "Reallocation 3");
        CHECK(allocations[95].offset != oa__NO_SPACE, "Reallocation 4");
        CHECK(allocations[151].offset != oa__NO_SPACE, "Reallocation 5 (4x)");

        for (oa_uint i = 0; i < 256; i++)
        {
            if (i < 152 || i > 154)
                oa_Free(gpu_allocator, allocations[i]);
        }

        oa_storage_report_t report2 = oa_StorageReport(gpu_allocator);
        CHECK(report2.totalFreeSpace == oa__MEGABYTE(256), "Storage report after free totalFreeSpace");
        CHECK(report2.largestFreeRegion == oa__MEGABYTE(256), "Storage report after free largestFreeRegion");

        oa_allocation_t validateAll = oa_Allocate(gpu_allocator, oa__MEGABYTE(256));
        CHECK(validateAll.offset == 0, "No fragmentation after bulk operations");
        oa_Free(gpu_allocator, validateAll);
        free(gpu_allocator);
    }

    printf("\n--- Test Summary ---\n");
    printf("%sPASS: %d%s\n", KGRN, g_pass_count, KNRM);
    printf("%sFAIL: %d%s\n", KRED, g_fail_count, KNRM);

    return g_fail_count > 0 ? 1 : 0;
}