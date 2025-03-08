/* file_ops.c - Implementation of file operations */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <dirent.h>

#include "../include/config.h"
#include "../include/file_ops.h"
#include "../include/logging.h"
#ifndef DT_REG
#define DT_REG 8  // Value for regular files
#endif

/* Create directory if it doesn't exist */
int create_directory_if_not_exists(const char *path) {
    struct stat st;
    
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0; // Directory already exists
        } else {
            log_message(CLOG_ERROR, "%s exists but is not a directory", path);
            return -1;
        }
    }
    
    if (mkdir(path, 0755) != 0) {
        log_message(CLOG_ERROR, "Failed to create directory %s: %s", path, strerror(errno));
        return -1;
    }
    
    log_message(CLOG_INFO, "Created directory %s", path);
    return 0;
}

/* Get username from UID */
char *get_username_from_uid(uid_t uid) {
    struct passwd *pwd;
    char *username;
    
    pwd = getpwuid(uid);
    if (pwd == NULL) {
        log_message(CLOG_WARNING, "Failed to get username for UID %d: %s", uid, strerror(errno));
        username = strdup("unknown");
    } else {
        username = strdup(pwd->pw_name);
    }
    
    return username;
}

/* Get formatted time string from timestamp */
char *get_time_string(time_t timestamp) {
    char *time_str = malloc(64);
    struct tm *time_info;
    
    time_info = localtime(&timestamp);
    strftime(time_str, 64, "%Y-%m-%d %H:%M:%S", time_info);
    
    return time_str;
}

/* Log file change to the change log file */
void log_file_change(const char *filename, const char *username, const char *timestamp) {
    FILE *fp;
    
    fp = fopen(CHANGE_LOG_FILE, "a");
    if (fp == NULL) {
        log_message(CLOG_ERROR, "Failed to open change log file: %s", strerror(errno));
        return;
    }
    
    fprintf(fp, "%s,%s,%s\n", filename, username, timestamp);
    fclose(fp);
}

/* Count files in directory matching pattern */
int count_files_in_dir(const char *dir_path, const char *pattern) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    
    dir = opendir(dir_path);
    if (dir == NULL) {
        log_message(CLOG_ERROR, "Failed to open directory %s: %s", dir_path, strerror(errno));
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            if (pattern == NULL || strstr(entry->d_name, pattern) != NULL) {
                count++;
            }
        }
    }
    
    closedir(dir);
    return count;
}