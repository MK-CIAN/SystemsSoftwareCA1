/* file_ops.h - Functions for file operations */

#ifndef FILE_OPS_H
#define FILE_OPS_H

#include <sys/types.h>

/* Create directory if it doesn't exist */
int create_directory_if_not_exists(const char *path);

/* Get username from UID */
char *get_username_from_uid(uid_t uid);

/* Get formatted time string from timestamp */
char *get_time_string(time_t timestamp);

/* Log file change to the change log file */
void log_file_change(const char *filename, const char *username, const char *timestamp);

/* Count files in directory matching pattern */
int count_files_in_dir(const char *dir_path, const char *pattern);

void check_uploads();

/* Check for missing reports from departments */
void check_missing_reports();

/* Lock directories before backup/transfer operations */
int lock_directories();

/* Unlock directories after backup/transfer operations */
int unlock_directories();

/* Transfer XML reports from upload to report directory */
void transfer_reports();

/* Backup report directory */
void backup_reports();

#endif /* FILE_OPS_H */