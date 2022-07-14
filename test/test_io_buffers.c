#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include "../src/io_buffers.h"


volatile int     g_exit = 0;
volatile int     g_break = 0;
static uint64_t  g_write_bytes = 0;
static uint64_t  g_read_bytes = 0;


static void init_rand(void)
{
    time_t tm;
    unsigned int n = (unsigned int)time(&tm);
    n = 1;
    srand(n);
}


static void on_signal_event(int signal)
{
    switch(signal)
    {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
    case SIGKILL:
        g_break = 1;
        break;
    }
}

static void handle_signals(void)
{
    struct sigaction act;
    memset((void *)&act,0,sizeof(struct sigaction));
    act.sa_handler = on_signal_event;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGHUP,  &act, NULL);
    sigaction(SIGKILL, &act, NULL);
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
}


static inline int is_data_valid(const uint8_t* data, uint32_t data_size)
{
    uint8_t c = (uint8_t)(data_size & 0xff);
    for(uint32_t i=0; i<data_size; i++)
    {
        if(data[i] != c) {
            return 0;
        }
    }
    return 1;
}


static void thread_main(struct IOBuffers* io_buffers)
{
    const uint8_t* p;
    uint32_t data_size = 0;

    for(;;)
    {
        p = try_read_io_buffers(io_buffers, &data_size);
        if(p)
        {
            if(is_data_valid(p, data_size)) {
                g_read_bytes += data_size;
            }

            read_io_buffers(io_buffers);
        }
        else if(g_exit) {
            break;
        }
    }
}


int main(int argc, char** argv)
{
    struct IOBuffers io_buffers;
    pthread_t read_thread;

    handle_signals();
    init_rand();

    printf("test io buffers, Ctrl+C to break\n");
    init_io_buffers(&io_buffers);

    for(int i=0; i<4; i++) {
        io_buffers_add_block(&io_buffers, 1024 * 1024, NULL);
    }

    pthread_create(&read_thread, NULL, (void*)thread_main, &io_buffers);

    while(!g_break)
    {
        uint32_t sz = rand() % 100;
        if(sz < 4) {
            sz = 4;
        }

        uint8_t* p = try_write_io_buffers(&io_buffers, sz);
        if(p)
        {
            memset(p, (uint8_t)(sz & 0xff), sz);
            write_io_buffers(&io_buffers);
            g_write_bytes += sz;
        }
    }

    g_exit = 1;
    usleep(100000);
    pthread_join(read_thread, NULL);
    cleanup_io_buffers(&io_buffers, NULL);

    if(g_read_bytes != g_write_bytes) {
        printf("read/write bytes not match, read bytes: %lu, write bytes: %lu\n", g_read_bytes, g_write_bytes);
    }
    else {
        printf("read/write match, bytes: %lu\n", g_read_bytes);
    }

    return 0;
}
