#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stringapiset.h>
#include "dokan_log.h"

std::string wide2local(const wchar_t *s)
{
    int size = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, FALSE);
    std::string res;
    res.resize(size);
    size = WideCharToMultiByte(CP_ACP, 0, s, -1, res.data(), size, NULL, FALSE);
    return std::move(res);
}

FILE *open_log_file(void)
{
    static FILE *log_file;
    if (!log_file)
    {
        log_file = fopen(set_log_name(NULL), "w");
        if (!log_file)
        {
            fprintf(stderr, "open log failed!\n");
            exit(EXIT_FAILURE);
        }
        setvbuf(log_file, NULL, _IOLBF, 0);
    }

    return log_file;
}

const char *set_log_name(const char *log_name)
{
    static char name[512] = "dokan.log";
    if (log_name)
    {
        strncpy(name, log_name, 511);
        name[511] = '\0';
    }
    return name;
}

void log_msg(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(open_log_file(), format, ap);
    va_end(ap);
}