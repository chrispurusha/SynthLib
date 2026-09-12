/*
 * SynthLib - offline checks of plugin/synthlibLog.c.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 * Licensed under the GNU General Public License v3 - see SynthLib's LICENSE.
 */

// The gate turns logging on and off without a reload, every line carries its writer, and lines from
// concurrent callers arrive whole. do-test builds this with ThreadSanitizer, which is the check that
// the gate cache is safe from any thread. Takes about two seconds: the gate is re-polled once a second.

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "synthlibLog.h"

const char gSynthLibLogName[] = "synthlib-logtest";

#define GATE_PATH        "/tmp/synthlib-logtest-log"
#define LOG_PATH         "/tmp/synthlib-logtest.log"
#define THREADS          (4)
#define LINES_PER_THREAD (250)

static int gFailures = 0;

#define CHECK(cond, ...)                          \
    do {                                          \
        if (!(cond)) {                            \
            fprintf(stderr, "logTest FAIL: ");    \
            fprintf(stderr, __VA_ARGS__);         \
            fputc('\n', stderr);                  \
            gFailures++;                          \
        }                                         \
    } while (0)

static void pause_seconds(double seconds) {
    usleep((useconds_t)(seconds * 1e6));
}

static void touch(const char * path) {
    FILE * file = fopen(path, "w");

    if (file != NULL) {
        fclose(file);
    }
}

// Every line in the log, checked against the prefix this process writes. Returns the count; `seen`
// (THREADS x LINES_PER_THREAD) is marked for each "thread T line N" found.
static int read_log(const char * prefix, bool * seen, int * hello, int * malformed) {
    FILE * file  = fopen(LOG_PATH, "r");
    char   line[512];
    int    count = 0;

    *hello     = 0;
    *malformed = 0;

    if (file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        int thread = -1;
        int n      = -1;

        count++;
        line[strcspn(line, "\n")] = '\0';

        if (strncmp(line, prefix, strlen(prefix)) != 0) {
            (*malformed)++;
            continue;
        }
        const char * body = line + strlen(prefix);

        if (strcmp(body, "hello 42") == 0) {
            (*hello)++;
        } else if ((sscanf(body, "thread %d line %d", &thread, &n) == 2)
                   && (thread >= 0) && (thread < THREADS) && (n >= 0) && (n < LINES_PER_THREAD)) {
            seen[(thread * LINES_PER_THREAD) + n] = true;
        } else {
            (*malformed)++;
        }
    }
    fclose(file);
    return count;
}

static void * writer(void * arg) {
    int thread = (int)(intptr_t)arg;

    for (int n = 0; n < LINES_PER_THREAD; n++) {
        synthlib_log_line("thread %d line %d", thread, n);
    }
    return NULL;
}

int main(void) {
    char      prefix[128];
    bool      seen[THREADS * LINES_PER_THREAD] = {false};
    int       hello                            = 0;
    int       malformed                        = 0;
    int       count                            = 0;
    pthread_t threads[THREADS];

    snprintf(prefix, sizeof(prefix), "[%s %d] ", getprogname(), (int)getpid());
    unlink(GATE_PATH);
    unlink(LOG_PATH);

    // 1. No gate, no file.
    synthlib_log_line("hello %d", 42);
    CHECK(access(LOG_PATH, F_OK) != 0, "a line was logged with the gate absent");

    // 2. The gate is noticed within a second, with no reload.
    touch(GATE_PATH);
    pause_seconds(1.1);
    synthlib_log_line("hello %d", 42);
    count = read_log(prefix, seen, &hello, &malformed);
    CHECK((count == 1) && (hello == 1) && (malformed == 0),
          "expected one \"%shello 42\" line after touching the gate, read %d lines (%d malformed)",
          prefix, count, malformed);

    // 3. Concurrent writers: every line arrives, whole and attributed.
    for (int t = 0; t < THREADS; t++) {
        pthread_create(&threads[t], NULL, writer, (void *)(intptr_t)t);
    }

    for (int t = 0; t < THREADS; t++) {
        pthread_join(threads[t], NULL);
    }
    count = read_log(prefix, seen, &hello, &malformed);
    CHECK(count == 1 + (THREADS * LINES_PER_THREAD), "expected %d lines, read %d",
          1 + (THREADS * LINES_PER_THREAD), count);
    CHECK(malformed == 0, "%d lines were torn or unattributed", malformed);

    for (int i = 0; i < THREADS * LINES_PER_THREAD; i++) {
        if (!seen[i]) {
            CHECK(false, "thread %d line %d is missing", i / LINES_PER_THREAD, i % LINES_PER_THREAD);
            break;
        }
    }

    // 4. Removing the gate turns it off again, also without a reload.
    unlink(GATE_PATH);
    pause_seconds(1.1);
    synthlib_log_line("hello %d", 42);
    CHECK(read_log(prefix, seen, &hello, &malformed) == count, "a line was logged after the gate was removed");

    unlink(LOG_PATH);

    if (gFailures == 0) {
        printf("logTest: gate on and off without a reload, %d concurrent lines whole and attributed\n",
               THREADS * LINES_PER_THREAD);
    }
    return (gFailures == 0) ? 0 : 1;
}
