#ifndef LOGGING_H
#define LOGGING_H

void log_hook(const char *func_name, const char *fmt, ...);
void log_msg(const char *fmt, ...);
int log_enabled(const char *func_name);

#endif /* LOGGING_H */
