#pragma once

#include <stdio.h>

enum log_level {
    LOG_ERR = '1',
    LOG_WARN = '2',
    LOG_INFO = '3',
    LOG_DEBUG = '4',
};

#define LOG(_level_, _fmt_, ...)                   \
    do {                                           \
        fprintf(stderr,                            \
                "%c:%s: "_fmt_                     \
                "\n",                              \
                _level_, __func__, ##__VA_ARGS__); \
    } while (0)
