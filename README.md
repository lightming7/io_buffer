# io_buffer
lock-free single-producer single-consumer ring buffer.

# io_buffers
lock-free single-producer single-consumer ring buffer which maintains several discontiguous memory blocks.

## Example

Producer:
```c
    uint8_t* p = try_write_io_buffer(io_buffer, 30);
    if(p)
    {
        memset(p, 'a', 30);
        write_io_buffer(io_buffer);
    }
```

Consumer:
```c
    uint32_t data_size;
    const uint8_t* p = try_read_io_buffer(io_buffer, &data_size);
    if(p)
    {
        hexdump(p, data_size);
        read_io_buffer(io_buffer);
    }
```
