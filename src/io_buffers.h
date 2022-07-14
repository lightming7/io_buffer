#pragma once

#ifndef TAILQ_ENTRY
#   include <sys/queue.h>
#endif

#define IO_BUFFERS_CACHE_LINE_SIZE                  64
#define IO_BUFFERS_ALIGN_CACHE_LINE                 __attribute__ ((aligned(IO_BUFFERS_CACHE_LINE_SIZE)))

#define IO_BUFFERS_CONS_POINTER(io_buffers)         (io_buffers->cons_hot_block->data + io_buffers->cons_hot_block->cons_pos)
#define IO_BUFFERS_CONS_DATA_SIZE(io_buffers)       *((uint32_t*)IO_BUFFERS_CONS_POINTER(io_buffers))
#define IO_BUFFERS_PROD_POINTER(io_buffers)         (io_buffers->prod_hot_block->data + io_buffers->prod_pos)

#define IO_BUFFERS_RECORD_ROOM_SIZE(data_size)      sizeof(uint32_t) + data_size + sizeof(uint32_t)      // record size descriptor, data_size, space/block end flag
#define IO_BUFFERS_BLOCK_END_FLAG                   0x80000000


struct IOBuffersBlock
{
    TAILQ_ENTRY(IOBuffersBlock) next;
    uint32_t    data_size;
    uint32_t    cons_pos;
    uint8_t     data[0];
};

TAILQ_HEAD(IOBuffersBlockHead, IOBuffersBlock);


// single producer single consumer IO buffers
struct IOBuffers
{
    struct IOBuffersBlockHead*  prod_blocks_head;
    struct IOBuffersBlock*      prod_hot_block;
    uint32_t                    prod_pos;
    uint32_t                    try_write_tail_pos;
    uint32_t                    try_write_size;
    char                        prod_padding IO_BUFFERS_ALIGN_CACHE_LINE;

    struct IOBuffersBlockHead*  cons_blocks_head;
    struct IOBuffersBlock*      cons_hot_block;

    struct IOBuffersBlockHead   block_head;
}IO_BUFFERS_ALIGN_CACHE_LINE;


static inline void init_io_buffers(struct IOBuffers* io_buffers)
{
    memset(io_buffers, 0, sizeof(struct IOBuffers));
    TAILQ_INIT(&io_buffers->block_head);
    io_buffers->prod_blocks_head = &io_buffers->block_head;
    io_buffers->cons_blocks_head = &io_buffers->block_head;
}


static inline int io_buffers_add_block(struct IOBuffers* io_buffers, uint32_t data_size, uint8_t* data)
{
    struct IOBuffersBlock* elem;

    if(data_size <= ((size_t)&((struct IOBuffersBlock*)0)->data) + 16) {    // offsetof(IOBuffersBlock::data)
        return -1;
    }

    if(data == NULL)
    {
        data = (uint8_t*)malloc(data_size);
        if(data == NULL) {
            return -2;
        }
    }

    memset(data, 0, data_size);
    ((struct IOBuffersBlock*)data)->data_size = data_size - ((size_t)&((struct IOBuffersBlock*)0)->data);

    TAILQ_INSERT_TAIL(&io_buffers->block_head, (struct IOBuffersBlock*)data, next);

    if(io_buffers->prod_hot_block == NULL) {
        io_buffers->prod_hot_block = TAILQ_FIRST(&io_buffers->block_head);
    }

    if(io_buffers->cons_hot_block == NULL) {
        io_buffers->cons_hot_block = TAILQ_FIRST(&io_buffers->block_head);
    }

    return 0;
}


static inline int io_buffers_has_block(struct IOBuffers* io_buffers)
{
    return TAILQ_EMPTY(&io_buffers->block_head) == 0;
}


static inline void cleanup_io_buffers(struct IOBuffers* io_buffers, void (*freer)(const void*))
{
    while(!TAILQ_EMPTY(&io_buffers->block_head))
    {
        struct IOBuffersBlock* elem = TAILQ_FIRST(&io_buffers->block_head);
        TAILQ_REMOVE(&io_buffers->block_head, elem, next);
        if(freer) {
            freer(elem);
        }
        else {
            free(elem);
        }
    }

    io_buffers->prod_hot_block = NULL;
    io_buffers->cons_hot_block = NULL;
}


static inline uint8_t* try_write_io_buffers(struct IOBuffers* io_buffers, uint32_t data_size)
{
    struct IOBuffersBlock* cons_block;
    struct IOBuffersBlock* prod_block;

    uint32_t cons_pos;
    uint32_t prod_pos;
    int has_space = 0;

    if(data_size == 0) {
        return NULL;
    }

    cons_block = io_buffers->cons_hot_block;
    prod_block = io_buffers->prod_hot_block;
    cons_pos = cons_block->cons_pos;

    io_buffers->try_write_tail_pos = 0;

    prod_pos = io_buffers->prod_pos;

    if(cons_block == prod_block && cons_pos > prod_pos)
    {
        //         p         c
        // | x x x . . . . . x x |
        if(cons_pos >= prod_pos + IO_BUFFERS_RECORD_ROOM_SIZE(data_size)) {
            // has enough space between [prod_pos -> cons_pos]
            has_space = 1;
        }
    }
    else
    {
        //     c   p
        // | . x x . . . . . . . |
        if(prod_block->data_size >= prod_pos + IO_BUFFERS_RECORD_ROOM_SIZE(data_size))
        {
            // has enough space between [prod_pos -> block end]
            has_space = 1;
        }
        else
        {
            prod_block = TAILQ_NEXT(prod_block, next);
            if(prod_block == NULL) {
                prod_block = TAILQ_FIRST(io_buffers->prod_blocks_head);
            }

            if(cons_block == prod_block)
            {
                //   p           c 
                // | . . . . . . x x . . | 
                if(IO_BUFFERS_RECORD_ROOM_SIZE(data_size) <= cons_pos)
                {
                    // has enough space between [block begin -> cons_pos]
                    io_buffers->try_write_tail_pos = prod_pos;
                    has_space = 1;
                }
            }
            else    // empty block
            {
                if(prod_block->data_size >= IO_BUFFERS_RECORD_ROOM_SIZE(data_size))
                {
                    // has enough space in next block
                    io_buffers->try_write_tail_pos = prod_pos;
                    has_space = 1;
                }
            }
            prod_pos = 0;
        }
    }

    if(has_space)
    {
        io_buffers->try_write_size = data_size;
        return (prod_block->data + prod_pos) + sizeof(uint32_t);
    }
    return 0;
}


static inline void write_io_buffers(struct IOBuffers* io_buffers)
{
    struct IOBuffersBlock* prod_block = io_buffers->prod_hot_block;
    uint32_t data_size = io_buffers->try_write_size;
    uint32_t aligned_data_size = data_size;
    if((aligned_data_size & 3) != 0) {
        aligned_data_size += 4 - (aligned_data_size & 3);
    }

    if(io_buffers->try_write_tail_pos)
    {
        struct IOBuffersBlock* next_prod_block;

        *((uint32_t*)(prod_block->data + io_buffers->try_write_tail_pos)) = IO_BUFFERS_BLOCK_END_FLAG; // has record at next block begin

        // switch to next block
        next_prod_block = TAILQ_NEXT(io_buffers->prod_hot_block, next);
        if(next_prod_block == NULL) {
            io_buffers->prod_hot_block = TAILQ_FIRST(io_buffers->prod_blocks_head);
        }
        else {
            io_buffers->prod_hot_block = next_prod_block;
        }
        io_buffers->prod_pos = 0;
    }

    *((uint32_t*)(IO_BUFFERS_PROD_POINTER(io_buffers) + sizeof(uint32_t) + aligned_data_size)) = 0;
    *((uint32_t*)(IO_BUFFERS_PROD_POINTER(io_buffers))) = data_size;
    io_buffers->prod_pos += sizeof(uint32_t) + aligned_data_size;

    if(io_buffers->try_write_tail_pos) {
        *((uint32_t*)(prod_block->data + io_buffers->try_write_tail_pos)) = IO_BUFFERS_BLOCK_END_FLAG; // has record at next block begin
    }
}


static inline const uint8_t* try_read_io_buffers(struct IOBuffers* io_buffers, uint32_t* data_size)
{
    *data_size = IO_BUFFERS_CONS_DATA_SIZE(io_buffers);
    if(*data_size == IO_BUFFERS_BLOCK_END_FLAG)
    {
        struct IOBuffersBlock* next_cons_block;
        next_cons_block = TAILQ_NEXT(io_buffers->cons_hot_block, next);
        if(next_cons_block == NULL) {
            next_cons_block = TAILQ_FIRST(io_buffers->cons_blocks_head);
        }

        *data_size = *(uint32_t*)(next_cons_block->data);
        if(*data_size) {
            return next_cons_block->data + sizeof(uint32_t);
        }
    }
    else if(*data_size) {
        return IO_BUFFERS_CONS_POINTER(io_buffers) + sizeof(uint32_t);
    }
    return NULL;
}


static inline void read_io_buffers(struct IOBuffers* io_buffers)
{
    uint32_t data_size = IO_BUFFERS_CONS_DATA_SIZE(io_buffers);
    if(data_size == IO_BUFFERS_BLOCK_END_FLAG)               // has record at begin
    {
        struct IOBuffersBlock* next_cons_block;
        IO_BUFFERS_CONS_DATA_SIZE(io_buffers) = 0;      // clear IO_BUFFERS_BLOCK_END_FLAG flag

        next_cons_block = TAILQ_NEXT(io_buffers->cons_hot_block, next);
        if(next_cons_block == NULL) {
            next_cons_block = TAILQ_FIRST(io_buffers->cons_blocks_head);
        }

        next_cons_block->cons_pos = 0;
        io_buffers->cons_hot_block = next_cons_block;

        data_size = IO_BUFFERS_CONS_DATA_SIZE(io_buffers);
    }

    if(data_size)
    {
        IO_BUFFERS_CONS_DATA_SIZE(io_buffers) = 0;
        if((data_size & 3) != 0) {
            data_size += 4 - (data_size & 3);
        }
        io_buffers->cons_hot_block->cons_pos += sizeof(uint32_t) + data_size;
    }
}
