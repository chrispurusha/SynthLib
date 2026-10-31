// Offline check of the swap fade: no step in the output, and a load inside the silence
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "synthlibSwapFade.h"

#define BLOCK    (64u)
#define RATE     (48000.0)
#define BLOCKS   (400)

static tSynthLibSwapFade gFade;
static float             gOut[BLOCK * BLOCKS];
static atomic_int        gBlock;

static void * audio(void * unused) {
    const struct timespec period = {0, 500000};

    (void)unused;

    for (int b = 0; b < BLOCKS; b++) {
        float * block = &gOut[(size_t)b * BLOCK];

        for (uint32_t f = 0; f < BLOCK; f++) {
            block[f] = 1.0f;
        }
        synthlib_swap_fade_apply(&gFade, block, NULL, BLOCK, RATE);
        atomic_store(&gBlock, b + 1);
        nanosleep(&period, NULL);
    }
    return NULL;
}

int main(void) {
    pthread_t             thread;
    const struct timespec settle = {0, 20000000};
    int                   failures = 0;

    synthlib_swap_fade_init(&gFade);
    pthread_create(&thread, NULL, audio, NULL);
    nanosleep(&settle, NULL);
    synthlib_swap_fade_begin(&gFade);
    int at = atomic_load(&gBlock);

    synthlib_swap_fade_end(&gFade);
    pthread_join(thread, NULL);

    // what was written after begin returned has to be silent
    failures += (gOut[(size_t)at * BLOCK] != 0.0f);
    float biggest = 0.0f, last = gOut[0], lowest = 1.0f;

    for (size_t i = 1; i < BLOCK * BLOCKS; i++) {
        biggest = (fabsf(gOut[i] - last) > biggest) ? fabsf(gOut[i] - last) : biggest;
        lowest  = (gOut[i] < lowest) ? gOut[i] : lowest;
        last    = gOut[i];
    }
    failures += (biggest > (float)(1.01 / (RATE * 0.005)));
    failures += (lowest != 0.0f) + (last != 1.0f);
    printf("swap fade: largest step %.5f, lowest %.3f, end %.3f, %s\n", biggest, lowest, last, (failures == 0) ? "pass" : "FAIL");

    // no audio thread at all: gives up rather than hanging
    tSynthLibSwapFade idle;

    synthlib_swap_fade_init(&idle);
    synthlib_swap_fade_begin(&idle);
    synthlib_swap_fade_end(&idle);
    return (failures == 0) ? 0 : 1;
}
