/* logging.h - Functions for logging */

#ifndef LOGGING_H
#define LOGGING_H

/* Initialize logging */
int init_logging();

/* Clean up logging */
void cleanup_logging();

/* Get log level string */
const char *get_log_level_str(int level);

/* Log a message */
void log_message(int level, const char *format, ...);

/* Log a system error */
void log_system_error(const char *message);

#endif /* LOGGING_H */