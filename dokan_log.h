#ifndef DOKAN_LOG_H
#define DOKAN_LOG_H

#define log_struct(st, field, format, ...) \
    log_msg("    " #field " = " format "\n", __VA_ARGS__(st->field))

const char *set_log_name(const char *name);

void log_msg(const char *format, ...);

#endif