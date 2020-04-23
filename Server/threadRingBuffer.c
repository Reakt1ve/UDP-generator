#include "threadRingBuffer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

_Bool initRingBuffer (struct ThreadRingBuffer *ringBuff, int size, int block_size) {
    ringBuff->block_size = block_size;
    ringBuff->ring_buffer = (char **) malloc (sizeof(char *) * size);
    if (ringBuff->ring_buffer == NULL) {
        return 0;
    }
    for (int i = 0; i < size; i++) {
        ringBuff->ring_buffer[i] = (char *) malloc (sizeof(char) * block_size);
        if (ringBuff->ring_buffer[i] == NULL) {
            return 0;
        }
    }

    ringBuff->buf_size = (char **) malloc (sizeof(char *));
    if (ringBuff->buf_size == NULL) {
        return 0;
    }
    ringBuff->buf_size[0] = (char *) malloc (sizeof(char) * block_size);
    if (ringBuff->buf_size[0] == NULL) {
        return 0;
    }
    ringBuff->buf_size = ringBuff->ring_buffer + size - 1;

    ringBuff->reader = (char **) malloc (sizeof(char *));
    if (ringBuff->reader == NULL) {
        return 0;
    }
    ringBuff->reader[0] = (char *) malloc (sizeof(char) * block_size);
    if (ringBuff->reader[0] == NULL) {
        return 0;
    }
    ringBuff->reader = ringBuff->ring_buffer;


    ringBuff->writter = (char **) malloc (sizeof(char *));
    if (ringBuff->writter == NULL) {
        return 0;
    }
    ringBuff->writter[0] = (char *) malloc (sizeof(char) * block_size);
    if(ringBuff->writter[0] == NULL) {
        return 0;
    }
    ringBuff->writter = ringBuff->ring_buffer + 1;

    //printf("sizeof: %lu",sizeof(char *));
    //printf("\nreader: %p  writter: %p  start: %p  finish: %p\n", ringBuff->reader[0], ringBuff->writter[0], ringBuff->ring_buffer[0], ringBuff->buf_size[0]);

    return 1;
}

_Bool pairingIsHappened (struct ThreadRingBuffer *ringBuff, enum pairing pr) {
    if (pr == READER_WRITTER) {
        if ((ringBuff->reader + 1) == ringBuff->writter) {
            return 1;
        } else {
            return 0;
        }
    }
    else if (pr == WRITTER_READER) {
        if ((ringBuff->writter + 1) == ringBuff->reader) {
            return 1;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
}

enum ring_buff_state getPackageRingBuffer (struct ThreadRingBuffer *ringBuff, char **buffer) {
    if ((ringBuff->reader + 1) == ringBuff->writter ||
        (ringBuff->reader == ringBuff->buf_size && ringBuff->writter == ringBuff->ring_buffer)) {
        return UNDERRIDE;
    } else {

        if (ringBuff->reader == ringBuff->buf_size) {
            ringBuff->reader = ringBuff->ring_buffer;
        } else {
            ringBuff->reader++;
        }

        *buffer = ringBuff->reader[0];

        return NON_CRITICAL;
    }
}

enum ring_buff_state putPackageRingBuffer (struct ThreadRingBuffer *ringBuff, char *buffer) {
    //printf("\nreader: %p  writter: %p  start: %p  finish: %p\n", ringBuff->reader[0], ringBuff->writter[0], ringBuff->ring_buffer[0], ringBuff->buf_size[0]);
    memcpy(ringBuff->writter[0], buffer, ringBuff->block_size);
    if ((ringBuff->writter + 1) == ringBuff->reader ||
        (ringBuff->writter == ringBuff->buf_size && ringBuff->reader == ringBuff->ring_buffer)) {
        return OVERRIDE;
    } else {
        if (ringBuff->writter == ringBuff->buf_size) {
            ringBuff->writter = ringBuff->ring_buffer;
        } else {
            ringBuff->writter++;
        }

        return NON_CRITICAL;
    }
}
