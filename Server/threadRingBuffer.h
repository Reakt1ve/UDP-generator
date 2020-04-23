#ifndef THREADRINGBUFFER_H_INCLUDED
#define THREADRINGBUFFER_H_INCLUDED

enum ring_buff_state {OVERRIDE = 1, UNDERRIDE, NON_CRITICAL};
enum pairing {READER_WRITTER = 1, WRITTER_READER};

typedef struct ThreadRingBuffer {
    char **reader;
    char **writter;
    char **buf_size;
    char **ring_buffer;
    int block_size;
} ThreadRingBuffer;

_Bool initRingBuffer (struct ThreadRingBuffer *, int, int);
_Bool pairingIsHappened (struct ThreadRingBuffer *, enum pairing);
enum ring_buff_state putPackageRingBuffer (struct ThreadRingBuffer *, char *);
enum ring_buff_state getPackageRingBuffer (struct ThreadRingBuffer *, char **);


#endif // THREADRINGBUFFER_H_INCLUDED
