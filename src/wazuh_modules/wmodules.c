/*
 * Wazuh Module Manager
 * Copyright (C) 2016 Wazuh Inc.
 * April 27, 2016.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include "wmodules.h"

wmodule *wmodules = NULL;   // Config: linked list of all modules.
int wm_task_nice = 0;       // Nice value for tasks.
int wm_max_eps;

// Read XML configuration and internal options

int wm_config() {

    // Get defined values from internal_options

    wm_task_nice = getDefine_Int("wazuh_modules", "task_nice", -20, 19);
    wm_max_eps = getDefine_Int("wazuh_modules", "max_eps", 100, 1000);

    // Read configuration: ossec.conf

    if (ReadConfig(CWMODULE, DEFAULTCPATH, &wmodules, NULL) < 0){
        return -1;
    }

#ifdef CLIENT
    // Read configuration: agent.conf
    ReadConfig(CWMODULE | CAGENT_CONFIG, AGENTCONFIG, &wmodules, NULL);
#else
    wmodule * database;
    // The database module won't be available on agents
    if ((database = wm_database_read())) {
        wm_add(database);
    }
#endif

    return 0;
}

// Add module to the global list

void wm_add(wmodule *module) {
    wmodule *current;

    if (wmodules) {
        for (current = wmodules; current->next; current = current->next);
        current->next = module;
    } else
        wmodules = module;
}

// Check general configuration

void wm_check() {
    wmodule *i;
    wmodule *j;
    wmodule *next;

    // Check that a configuration exists

    if (!wmodules) {
        minfo("No configuration defined. Exiting...");
        exit(EXIT_SUCCESS);
    }

    // Get the last module of the same type

    for (i = wmodules->next; i; i = i->next) {
        for (j = wmodules; j != i; j = next) {
            next = j->next;

            if (i->context->name == j->context->name) {
                if (j->context->destroy)
                    j->context->destroy(j->data);

                free(j);

                if (j == wmodules)
                    wmodules = next;
            }
        }
    }
}

// Destroy configuration data

void wm_destroy() {
    wmodule *cur_module;
    wmodule *next_module;

    for (cur_module = wmodules; cur_module; cur_module = next_module) {
        next_module = cur_module->next;

        if (cur_module->context->destroy)
            cur_module->context->destroy(cur_module->data);

        free(cur_module);
    }
}

// Concatenate strings with optional separator

int wm_strcat(char **str1, const char *str2, char sep) {
    size_t len1;
    size_t len2;

    if (str2) {
        len2 = strlen(str2);

        if (*str1) {
            len1 = strlen(*str1);
            os_realloc(*str1, len1 + len2 + (sep ? 2 : 1), *str1);

            if (sep)
                memcpy(*str1 + (len1++), &sep, 1);
        } else {
            len1 = 0;
            os_malloc(len2 + 1, *str1);
        }

        memcpy(*str1 + len1, str2, len2 + 1);
        return 0;
    } else
        return -1;
}

// Tokenize string separated by spaces, respecting double-quotes

char** wm_strtok(char *string) {
    char *c = string;
    char **output = (char**)calloc(2, sizeof(char*));
    size_t n = 1;

    if (!output)
        return NULL;

    *output = string;

    while ((c = strpbrk(c, " \"\\"))) {
        switch (*c) {
        case ' ':
            *(c++) = '\0';
            output[n++] = c;
            output = (char**)realloc(output, (n + 1) * sizeof(char*));
            output[n] = NULL;
            break;

        case '\"':
            c++;

            while ((c = strpbrk(c, "\"\\"))) {
                if (*c == '\\')
                    c += 2;
                else
                    break;
            }

            if (!c) {
                free(output);
                return NULL;
            }

            c++;
            break;

        case '\\':
            c += 2;
        }
    }

    return output;
}

// Load or save the running state

int wm_state_io(const wm_context *context, int op, void *state, size_t size) {
    char path[PATH_MAX] = { '\0' };
    size_t nmemb;
    FILE *file;

    #ifdef WIN32
    snprintf(path, PATH_MAX, "%s\\%s", WM_STATE_DIR_WIN, context->name);
    #else
    snprintf(path, PATH_MAX, "%s/%s", WM_STATE_DIR, context->name);
    #endif

    if (!(file = fopen(path, op == WM_IO_WRITE ? "wb" : "rb"))){
        merror("Unable to open the file: %s", path);
        return -1;
    }

    nmemb = (op == WM_IO_WRITE) ? fwrite(state, size, 1, file) : fread(state, size, 1, file);
    fclose(file);

    return nmemb - 1;
}
