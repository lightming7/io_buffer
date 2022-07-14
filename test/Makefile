OBJ:= test_io_buffer test_io_buffers

CC:= gcc
CFLAGS:=
LDFLAGS:=

.PHONY: all 

all:
	${CC} -o test_io_buffer ${CFLAGS} ${LDFLAGS} test_io_buffer.c
	${CC} -o test_io_buffers ${CFLAGS} ${LDFLAGS} test_io_buffers.c

clean:
	rm -fr ${OBJ} *.o
