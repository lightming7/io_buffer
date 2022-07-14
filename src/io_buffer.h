#pragma once

#define IO_BUFFER_HEAD_POINTER(io_buffer)       (io_buffer->data_cons + io_buffer->head)
#define IO_BUFFER_TAIL_POINTER(io_buffer)       (io_buffer->data_prod + io_buffer->tail)
#define IO_BUFFER_HEAD_DATA_SIZE(io_buffer)     *((uint32_t*)IO_BUFFER_HEAD_POINTER(io_buffer))
#define IO_BUFFER_LAST_POSITION(io_buffer)      (io_buffer->buffer_size - (sizeof(uint32_t)))
#define IO_BUFFER_BLOCK_END_FLAG                0x80000000


// single producer single consumer IO buffer
struct IOBuffer
{
    uint8_t*        malloc_buffer;  // read only after init, malloc buffer pointer
    uint8_t*        data_cons;      // read only after init
    uint32_t        head;           // update by reader, save head position offset by the data
    char            padding __attribute__ ((aligned(64)));

    size_t          buffer_size;    // read only after init
    uint8_t*        data_prod;      // read only after init
    uint32_t        tail;           // update by writer, save tail position offset by the data
    uint32_t        last_tail;      // temporary tail for try_write
    uint32_t        try_write_size; // write size for try_write
}__attribute__ ((aligned(64)));


inline static int attach_io_buffer(struct IOBuffer* io_buffer, size_t buffer_size, uint8_t* data)
{
    memset(io_buffer, 0, sizeof(struct IOBuffer));
    io_buffer->buffer_size = buffer_size;

    if(data)
    {
        io_buffer->data_cons = data;
        io_buffer->malloc_buffer = NULL;
    }
    else
    {
        io_buffer->data_cons = (uint8_t*)calloc(1, buffer_size);
        io_buffer->malloc_buffer = io_buffer->data_cons;
    }

    if(io_buffer->data_cons)
    {
        io_buffer->data_prod = io_buffer->data_cons;
        return 1;
    }
    return 0;
}

inline static int init_io_buffer(struct IOBuffer* io_buffer, size_t buffer_size, uint8_t* data)
{
    int ok = attach_io_buffer(io_buffer, buffer_size, data);
    if(ok)
    {
        io_buffer->head = 0;
        io_buffer->tail = 0;
        *((uint32_t*)(IO_BUFFER_TAIL_POINTER(io_buffer))) = 0;
    }
    return ok;
}

inline static void cleanup_io_buffer(struct IOBuffer* io_buffer)
{
    if(io_buffer->malloc_buffer)
    {
        free(io_buffer->malloc_buffer);
        io_buffer->malloc_buffer = NULL;
    }
}

inline static void clear_io_buffer_for_reader(struct IOBuffer* io_buffer)
{
    io_buffer->head = io_buffer->tail;
}

inline static void clear_io_buffer_for_writer(struct IOBuffer* io_buffer)
{
    io_buffer->tail = io_buffer->head;
}

inline static const uint8_t* try_read_io_buffer(struct IOBuffer* io_buffer, uint32_t* data_bytes)
{
    uint32_t data_size = IO_BUFFER_HEAD_DATA_SIZE(io_buffer);
    if(data_size == IO_BUFFER_BLOCK_END_FLAG)     // has record at begin
    {
        io_buffer->head = 0;
        data_size = IO_BUFFER_HEAD_DATA_SIZE(io_buffer);
    }
    if(data_size)
    {
        *data_bytes = data_size;
        return IO_BUFFER_HEAD_POINTER(io_buffer) + sizeof(uint32_t);
    }
    return NULL;
}

inline static void read_io_buffer(struct IOBuffer* io_buffer)
{
    uint32_t data_size = IO_BUFFER_HEAD_DATA_SIZE(io_buffer);
    if(data_size == IO_BUFFER_BLOCK_END_FLAG)     // has record at begin
    {
        IO_BUFFER_HEAD_DATA_SIZE(io_buffer) = 0;    // clear IO_BUFFER_BLOCK_END_FLAG flag
        io_buffer->head = 0;
        data_size = IO_BUFFER_HEAD_DATA_SIZE(io_buffer);
    }
    if(data_size)
    {
        IO_BUFFER_HEAD_DATA_SIZE(io_buffer) = 0;
        if((data_size & 3) != 0) {
            data_size += 4 - (data_size & 3);
        }
        io_buffer->head += sizeof(uint32_t) + data_size;
    }
}

// max data_size 0x7fffffff
inline static uint8_t* try_write_io_buffer(struct IOBuffer* io_buffer, uint32_t data_size)
{
    uint32_t head = io_buffer->head;
    uint32_t tail = io_buffer->tail;

    if(data_size == 0) {
        return NULL;
    }

    io_buffer->last_tail = 0;

    if(head > tail && head > tail + (sizeof(uint32_t) + sizeof(uint32_t)) + data_size) {
        // has enough space between [tail -> head]
    }
    else if(head <= tail && IO_BUFFER_LAST_POSITION(io_buffer) > tail + data_size) {
        // has enough space between [tail -> buffer end]
    }
    else if(head <= tail && head > (sizeof(uint32_t) + sizeof(uint32_t)) + data_size)
    {
        // has enough space between [buffer begin -> head]
        io_buffer->last_tail = tail;
        io_buffer->tail = 0;
        tail = 0;
    }
    else
    {
        // buffer full
        return 0;
    }

    io_buffer->try_write_size = data_size;
    return (io_buffer->data_prod + tail) + sizeof(uint32_t);
}

inline static void write_io_buffer(struct IOBuffer* io_buffer)
{
    uint32_t data_size = io_buffer->try_write_size;
    uint32_t aligned_data_size = data_size;
    if((aligned_data_size & 3) != 0)
        aligned_data_size += 4 - (aligned_data_size & 3);

    *((uint32_t*)(IO_BUFFER_TAIL_POINTER(io_buffer) + sizeof(uint32_t) + aligned_data_size)) = 0;
    *((uint32_t*)(IO_BUFFER_TAIL_POINTER(io_buffer))) = data_size;
    io_buffer->tail += sizeof(uint32_t) + aligned_data_size;

    if(io_buffer->last_tail) {
        *((uint32_t*)(io_buffer->data_prod + io_buffer->last_tail)) = IO_BUFFER_BLOCK_END_FLAG; // has record at begin
    }
}
