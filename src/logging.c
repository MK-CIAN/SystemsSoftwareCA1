/* logging.c - Implementation of logging functionality */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "../include/config.h"
#include "../include/logging.h"

static FILE *log_fp = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize logging
int init_logging() {
    // Creating the log directory if it doesn't exist
    struct stat st;
    if (stat(LOG_DIR, &st) != 0) {
        if (mkdir(LOG_DIR, 0755) != 0) {
            fprintf(stderr, "Failed to create log directory: %s\n", strerror(errno));
            return -1;
        }
    }
    
    // Open log file
    log_fp = fopen(LOG_FILE, "a");
    if (log_fp == NULL) {
        fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
        return -1;
    }
    
    // Set buffer to line buffered
    setvbuf(log_fp, NULL, _IOLBF, 0);
    
    return 0;
}

// Cleaning up logging
void cleanup_logging() {
    if (log_fp != NULL) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

// Geting log level string
const char *get_log_level_str(int level) {
    switch (level) {
        case CLOG_DEBUG:
            return "DEBUG";
        case CLOG_INFO:
            return "INFO";
        case CLOG_WARNING:
            return "WARNING";
        case CLOG_ERROR:
            return "ERROR";
        case CLOG_CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

// Logging a message
void log_message(int level, const char *format, ...) {
    va_list args;
    time_t now;
    struct tm *time_info;
    char timestamp[20];
    
    // Check if log level is enabled
    if (level < LOG_LEVEL) {
        return;
    }
    
    // Get current time
    time(&now);
    time_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
    
    // Lock mutex
    pthread_mutex_lock(&log_mutex);
    
    // Log to file if available
    if (log_fp != NULL) {
        fprintf(log_fp, "[%s] [%s] ", timestamp, get_log_level_str(level));
        va_start(args, format);
        vfprintf(log_fp, format, args);
        va_end(args);
        fprintf(log_fp, "\n");
        fflush(log_fp);
    }
    
    // Also log to stderr for ERROR and CRITICAL
    if (level >= CLOG_ERROR) {
        fprintf(stderr, "[%s] [%s] ", timestamp, get_log_level_str(level));
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
    
    // Unlock mutex
    pthread_mutex_unlock(&log_mutex);
}

// Logging a system error
void log_system_error(const char *message) {
    log_message(CLOG_ERROR, "%s: %s", message, strerror(errno));
}