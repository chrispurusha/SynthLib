/*
 * SynthLib - a plain-text command channel into a plug-in, for scripts and assistants.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// Notes: Docs/code-notes/synthlibBackdoor.md

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synthlibBackdoor.h"

#define POLL_EVERY    (6)         // frames: a few times a second
#define MAX_COMMAND   (65536)

static const tSynthLibParam * find_param(const tSynthLibBackdoor * ops, const char * title) {
    for (uint32_t i = 0; i < ops->count; i++) {
        if ((ops->params[i].title != NULL) && (strcasecmp(ops->params[i].title, title) == 0)) {
            return &ops->params[i];
        }
    }
    return NULL;
}

static double to_plain(const tSynthLibParam * p, double normalized) {
    return p->plainMin + (normalized * (p->plainMax - p->plainMin));
}

static void show_param(FILE * out, const tSynthLibBackdoor * ops, void * user, const tSynthLibParam * p) {
    double normalized = ops->get(user, p->id);
    char   text[128]  = "";

    if ((ops->text == NULL) || !ops->text(user, p->id, normalized, text, sizeof(text))) {
        text[0] = '\0';
    }
    fprintf(out, "%s = %g (%g..%g) %s\n", p->title, to_plain(p, normalized), p->plainMin, p->plainMax, text);
}

// notes §2: TITLE=VALUE, the value plain, or a choice as its text
static void set_param(FILE * out, const tSynthLibBackdoor * ops, void * user, char * body) {
    char * equals = strchr(body, '=');

    if ((equals == NULL) || (ops->set == NULL)) {
        fprintf(out, "ERROR: SET title=value\n");
        return;
    }
    *equals = '\0';
    char *                 value = equals + 1;
    const tSynthLibParam * p     = NULL;

    for (char * end = equals; (end > body) && isspace((unsigned char)end[-1]); end--) {
        end[-1] = '\0';
    }

    while (isspace((unsigned char)*value)) {
        value++;
    }
    p = find_param(ops, body);

    if (p == NULL) {
        fprintf(out, "ERROR: no parameter \"%s\"\n", body);
        return;
    }
    char * rest  = NULL;
    double plain = strtod(value, &rest);
    double normalized;

    if ((rest != value) && (*rest == '\0')) {
        normalized = (plain - p->plainMin) / (p->plainMax - p->plainMin);
    } else {
        int  steps = (p->stepCount > 0) ? p->stepCount : 0;
        bool found = false;

        normalized = 0.0;

        for (int s = 0; (s <= steps) && !found && (ops->text != NULL); s++) {
            char   text[128];
            double n = (steps > 0) ? ((double)s / steps) : 0.0;

            if (ops->text(user, p->id, n, text, sizeof(text)) && (strcasecmp(text, value) == 0)) {
                normalized = n;
                found      = true;
            }
        }

        if (!found) {
            fprintf(out, "ERROR: \"%s\" is no value of %s\n", value, p->title);
            return;
        }
    }
    normalized = (normalized < 0.0) ? 0.0 : ((normalized > 1.0) ? 1.0 : normalized);

    if (p->stepCount > 0) {
        normalized = round(normalized * p->stepCount) / p->stepCount;
    }
    ops->set(user, p->id, normalized);
    show_param(out, ops, user, p);
}

static void run_line(FILE * out, const char * name, const tSynthLibBackdoor * ops, void * user, char * line) {
    char * body = line;

    while ((*body != '\0') && !isspace((unsigned char)*body)) {
        body++;
    }

    if (*body != '\0') {
        *body++ = '\0';

        while (isspace((unsigned char)*body)) {
            body++;
        }
    }

    if (strcasecmp(line, "HELP") == 0) {
        fprintf(out, "LIST [word] | CHANGED | GET title | SET title=value | NAME [text] | PRESET n | SAVE path | INIT | HELP\n");
    } else if (strcasecmp(line, "LIST") == 0) {
        for (uint32_t i = 0; i < ops->count; i++) {
            if ((*body == '\0') || (strcasestr(ops->params[i].title, body) != NULL)) {
                show_param(out, ops, user, &ops->params[i]);
            }
        }
    } else if (strcasecmp(line, "CHANGED") == 0) {
        for (uint32_t i = 0; i < ops->count; i++) {
            if (fabs(ops->get(user, ops->params[i].id) - ops->params[i].defaultNormalized) > 1e-6) {
                show_param(out, ops, user, &ops->params[i]);
            }
        }
    } else if (strcasecmp(line, "GET") == 0) {
        const tSynthLibParam * p = find_param(ops, body);

        if (p != NULL) {
            show_param(out, ops, user, p);
        } else {
            fprintf(out, "ERROR: no parameter \"%s\"\n", body);
        }
    } else if (strcasecmp(line, "SET") == 0) {
        set_param(out, ops, user, body);
    } else if ((strcasecmp(line, "NAME") == 0) && (*body == '\0') && (ops->name != NULL)) {
        fprintf(out, "%s\n", ops->name(user));
    } else if ((strcasecmp(line, "NAME") == 0) && (ops->rename != NULL)) {
        ops->rename(user, body);
        fprintf(out, "OK\n");
    } else if ((strcasecmp(line, "PRESET") == 0) && (ops->preset != NULL)) {
        fprintf(out, ops->preset(user, atoi(body)) ? "OK\n" : "ERROR: no such preset\n");
    } else if ((strcasecmp(line, "SAVE") == 0) && (ops->save != NULL)) {
        fprintf(out, ops->save(user, body) ? "OK\n" : "ERROR: not written\n");
    } else if ((strcasecmp(line, "INIT") == 0) && (ops->init != NULL)) {
        ops->init(user);
        fprintf(out, "OK\n");
    } else {
        fprintf(out, "ERROR: %s: unknown or not offered by %s\n", line, name);
    }
}

void synthlib_backdoor_poll(const char * name, const tSynthLibBackdoor * ops, void * user) {
    static int  frames;
    char        gate[256], command[256], result[256], partial[280];
    struct stat st;

    if (++frames < POLL_EVERY) {
        return;
    }
    frames = 0;
    snprintf(gate, sizeof(gate), "/tmp/%s-backdoor", name);

    // notes §1
    if ((stat(gate, &st) != 0) || (st.st_uid != getuid())) {
        return;
    }
    snprintf(command, sizeof(command), "/tmp/%s_cmd.txt", name);
    snprintf(result, sizeof(result), "/tmp/%s_result.txt", name);
    snprintf(partial, sizeof(partial), "%s.part", result);

    if ((stat(command, &st) != 0) || (st.st_uid != getuid()) || (st.st_size > MAX_COMMAND)) {
        return;
    }
    FILE * in  = fopen(command, "r");
    FILE * out = fopen(partial, "w");

    if ((in != NULL) && (out != NULL)) {
        char line[1024];

        while (fgets(line, sizeof(line), in) != NULL) {
            line[strcspn(line, "\r\n")] = '\0';

            if (line[0] != '\0') {
                run_line(out, name, ops, user, line);
            }
        }
    }

    if (in != NULL) {
        fclose(in);
    }

    if (out != NULL) {
        fclose(out);
        rename(partial, result);
    }
    unlink(command);
}
