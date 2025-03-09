/* config.h - Configuration variables */

#ifndef CONFIG_H
#define CONFIG_H

#include <limits.h>
#include <sys/stat.h>

/* Daemon configuration */
#define PID_FILE "/var/run/report_daemon.pid"

/* Directory paths */
#define UPLOAD_DIR "/var/reports/upload"
#define REPORT_DIR "/var/reports/dashboard"
#define BACKUP_DIR "/var/backups/reports"
#define LOG_DIR "/var/log/report_daemon"

/* Log file */
#define LOG_FILE LOG_DIR "/report_daemon.log"
#define CHANGE_LOG_FILE LOG_DIR "/changes.log"

/* Log levels */
#undef LOG_DEBUG
#undef LOG_INFO
#undef LOG_WARNING
#define CLOG_DEBUG    0
#define CLOG_INFO     1
#define CLOG_WARNING  2
#define CLOG_ERROR    3
#define CLOG_CRITICAL 4

/* Current log level */
#define LOG_LEVEL CLOG_INFO

/* Directory permissions */
#define UPLOAD_DIR_PERMS (S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) /* 0775 */
#define REPORT_DIR_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH) /* 0664 */

/* Check interval in seconds */
#define CHECK_INTERVAL 60

/* Buffer size for IPC */
#define BUFFER_SIZE 4096

#endif