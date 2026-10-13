/* Fuzzes the LE executable loader (object table, page map, fixups). */
#include "bs/formats/le.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    bs_arena a;
    bs_arena_init(&a, 0);
    bs_le_image img;
    if (bs_le_load(data, size, &a, &img) == BS_OK) {
        /* Touch both ends of every object so ASan checks the reported sizes. */
        volatile uint8_t sink = 0;
        for (uint32_t i = 0; i < img.object_count; i++) {
            const bs_le_object *o = &img.objects[i];
            if (o->mem_size)
                sink ^= (uint8_t)(o->data[0] ^ o->data[o->mem_size - 1]);
        }
        (void)sink;
    }
    bs_arena_free(&a);
    return 0;
}
