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
#include <linux/limits.h>
#include <pthread.h>

#include "../include/config.h"
#include "../include/file_ops.h"
#include "../include/logging.h"
#ifndef DT_REG
#define DT_REG 8  // Value for regular files
#endif


pthread_mutex_t dir_mutex = PTHREAD_MUTEX_INITIALIZER;
// Creating the directory if it doesn't exist
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

// Getting the username from UID
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

// Getting the formatted time string from timestamp
char *get_time_string(time_t timestamp) {
    char *time_str = malloc(64);
    struct tm *time_info;
    
    time_info = localtime(&timestamp);
    strftime(time_str, 64, "%Y-%m-%d %H:%M:%S", time_info);
    
    return time_str;
}

// Logging the file change to the change log file
void log_file_change(const char *filename, const char *username, const char *timestamp) {
    FILE *fp;

    // Making sure the /var/log/report_daemon/ directory exists
    struct stat st;
    if (stat(LOG_DIR, &st) != 0) {
        if (mkdir(LOG_DIR, 0755) != 0) {
            log_message(CLOG_ERROR, "Failed to create log directory: %s", strerror(errno));
            return;
        }
    }

    // Ensure changes.log exists in LOG_DIR
    if (access(CHANGE_LOG_FILE, F_OK) != 0) {
        fp = fopen(CHANGE_LOG_FILE, "w");
        if (fp != NULL) {
            fprintf(fp, "File,User,Timestamp\n"); // CSV-style header
            fclose(fp);
        }
        // Set read-only permissions for log file
        chmod(CHANGE_LOG_FILE, S_IRUSR | S_IRGRP | S_IROTH);
    }

    // Open the file for appending logs
    fp = fopen(CHANGE_LOG_FILE, "a");
    if (fp == NULL) {
        log_message(CLOG_ERROR, "Failed to open change log file: %s", strerror(errno));
        return;
    }

    fprintf(fp, "%s,%s,%s\n", filename, username, timestamp);
    fclose(fp);
}


// Counting the files in directory matching pattern
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

//Check uploaded XML reports and log the changes, this goes to a changes_log text file in uploads folder
void check_uploads() {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char path[PATH_MAX];
    char *last_modified_time;
    char *username;
    
    dir = opendir(UPLOAD_DIR);
    if (dir == NULL) {
        log_message(CLOG_ERROR, "Failed to open upload directory: %s", strerror(errno));
        return;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".xml") != NULL) {
            snprintf(path, PATH_MAX, "%s/%s", UPLOAD_DIR, entry->d_name);
            
            if (stat(path, &file_stat) == 0) {
                // Check if file was modified in the last CHECK_INTERVAL seconds
                if (time(NULL) - file_stat.st_mtime < CHECK_INTERVAL) {
                    // Get username from UID
                    username = get_username_from_uid(file_stat.st_uid);
                    
                    // Get last modified time
                    last_modified_time = get_time_string(file_stat.st_mtime);
                    
                    log_message(CLOG_INFO, "XML file modified: %s by %s at %s", 
                                entry->d_name, username, last_modified_time);
                    
                    // Log change to the change log file
                    log_file_change(entry->d_name, username, last_modified_time);
                    
                    free(last_modified_time);
                    free(username);
                }
            }
        }
    }
    
    closedir(dir);
}

/* Check for missing reports from departments */
void check_missing_reports() {
    int warehouse_found = 0;
    int manufacturing_found = 0;
    int sales_found = 0;
    int distribution_found = 0;
    DIR *dir;
    struct dirent *entry;
    char today_date[20];
    time_t now = time(NULL);
    struct tm *time_info = localtime(&now);
    
    // Get yesterday's date
    time_info->tm_mday -= 1;  // Moving back one day
    mktime(time_info); 
    char yesterday_date[20];
    strftime(yesterday_date, sizeof(yesterday_date), "%Y%m%d", time_info);

    
    dir = opendir(UPLOAD_DIR);
    if (dir == NULL) {
        log_message(CLOG_ERROR, "Failed to open upload directory: %s", strerror(errno));
        return;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".xml") != NULL) {
            if (strstr(entry->d_name, "warehouse") && strstr(entry->d_name, yesterday_date))
                warehouse_found = 1;
            else if (strstr(entry->d_name, "manufacturing") && strstr(entry->d_name, yesterday_date))
                manufacturing_found = 1;
            else if (strstr(entry->d_name, "sales") && strstr(entry->d_name, yesterday_date))
                sales_found = 1;
            else if (strstr(entry->d_name, "distribution") && strstr(entry->d_name, yesterday_date))
                distribution_found = 1;
        }
    }
    
    closedir(dir);
    
    // Logging missing reports
    if (!warehouse_found)
        log_message(CLOG_WARNING, "Missing warehouse report for %s", today_date);
    if (!manufacturing_found)
        log_message(CLOG_WARNING, "Missing manufacturing report for %s", today_date);
    if (!sales_found)
        log_message(CLOG_WARNING, "Missing sales report for %s", today_date);
    if (!distribution_found)
        log_message(CLOG_WARNING, "Missing distribution report for %s", today_date);
}

/* Lock directories before backup/transfer */
int lock_directories() {
    // Restrict access to the upload directory (read-only for all)
    if (chmod(UPLOAD_DIR, S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        log_message(CLOG_ERROR, "Failed to lock upload directory: %s", strerror(errno));
        return -1;
    }

    // Restrict access to the dashboard directory (read-only for all)
    if (chmod(REPORT_DIR, S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        log_message(CLOG_ERROR, "Failed to lock report directory: %s", strerror(errno));
        return -1;
    }

    // Lock directory mutex
    int ret = pthread_mutex_lock(&dir_mutex);
    if (ret != 0) {
        log_message(CLOG_ERROR, "Failed to lock directories: %s", strerror(ret));
        return -1;
    }

    log_message(CLOG_INFO, "Directories locked for backup/transfer.");
    return 0;
}


// Locking directories before backup/transfer operations to prevent modifications
int unlock_directories() {
    // Ensure /var/reports/ allows access to subdirectories
    if (chmod("/var/reports", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        log_message(CLOG_ERROR, "Failed to set /var/reports permissions: %s", strerror(errno));
        return -1;
    }

    // 🔹 Fully writable upload directory (everyone has full access)
    if (chmod(UPLOAD_DIR, 0777) != 0) {
        log_message(CLOG_ERROR, "Failed to restore upload directory permissions: %s", strerror(errno));
        return -1;
    }

    // 🔹 Ensure all files in upload directory are writable by all
    system("chmod -R 777 /var/reports/upload/*.xml");

    // 🔹 Dashboard directory: Read-only for non-root users
    if (chmod(REPORT_DIR, 0755) != 0) {
        log_message(CLOG_ERROR, "Failed to set dashboard directory as read-only: %s", strerror(errno));
        return -1;
    }

    // 🔹 Ensure all files in dashboard are readable, but NOT writable by non-root users
    system("chmod -R 644 /var/reports/dashboard/*.xml");

    // Unlock directory mutex
    int ret = pthread_mutex_unlock(&dir_mutex);
    if (ret != 0) {
        log_message(CLOG_ERROR, "Failed to unlock directories: %s", strerror(ret));
        return -1;
    }

    log_message(CLOG_INFO, "Directories unlocked after backup/transfer. Permissions set for reports.");
    return 0;
}

// Transfer XML reports from upload to report directory
void transfer_reports() {
    pid_t pid;
    int status;
    int pipe_fd[2];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    // Create a pipe for IPC
    if (pipe(pipe_fd) == -1) {
        log_message(CLOG_ERROR, "Failed to create pipe: %s", strerror(errno));
        return;
    }
    
    // Lock directories before transfer
    if (lock_directories() != 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }
    
    pid = fork();
    
    if (pid < 0) {
        log_message(CLOG_ERROR, "Failed to fork for transfer: %s", strerror(errno));
        unlock_directories();
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    } else if (pid == 0) {
        // Child process
        close(pipe_fd[0]); // Close read end
        
        // Redirect stdout to pipe
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        
        // Executing the transfer operation and delete the file from upload after
        execl("/bin/sh", "sh", "-c", "find " UPLOAD_DIR " -name \"*.xml\" -exec cp {} " REPORT_DIR " \\; -exec echo \"Transferred: {}\" \\; -exec rm {} \\;", NULL);

        
        // If execl fails
        log_message(CLOG_ERROR, "Failed to execute transfer command: %s", strerror(errno));
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        close(pipe_fd[1]); // Close write end
        
        // Read output from child process
        while ((bytes_read = read(pipe_fd[0], buffer, BUFFER_SIZE - 1)) > 0) {
            buffer[bytes_read] = '\0';
            log_message(CLOG_INFO, "%s", buffer);
        }
        
        close(pipe_fd[0]);
        
        // Waiting for child to complete
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                log_message(CLOG_INFO, "Transfer completed successfully");
            } else {
                log_message(CLOG_ERROR, "Transfer failed with status %d", WEXITSTATUS(status));
            }
        } else {
            log_message(CLOG_ERROR, "Transfer process terminated abnormally");
        }
        
        // Unlocking directories after transfer
        unlock_directories();
    }
}

// Backup report directory
void backup_reports() {
    pid_t pid;
    int status;
    int pipe_fd[2];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char backup_dir[PATH_MAX];
    char timestamp[20];
    time_t now = time(NULL);
    struct tm *time_info = localtime(&now);
    
    // Creating timestamp for backup directory
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", time_info);
    snprintf(backup_dir, PATH_MAX, "%s/%s", BACKUP_DIR, timestamp);
    
    // Creating backup directory
    if (mkdir(backup_dir, 0755) != 0) {
        log_message(CLOG_ERROR, "Failed to create backup directory %s: %s", backup_dir, strerror(errno));
        return;
    }
    
    // Creating a pipe for IPC
    if (pipe(pipe_fd) == -1) {
        log_message(CLOG_ERROR, "Failed to create pipe: %s", strerror(errno));
        return;
    }
    
    // Locking directories before backup
    if (lock_directories() != 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    }
    
    pid = fork();
    
    if (pid < 0) {
        log_message(CLOG_ERROR, "Failed to fork for backup: %s", strerror(errno));
        unlock_directories();
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return;
    } else if (pid == 0) {
        // Child process
        close(pipe_fd[0]); // Close read end
        
        // Redirecting stdout to pipe
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        
        // Executing the backup operation
        char command[PATH_MAX];
        snprintf(command, sizeof(command), "cp -r %s/*.xml %s/ 2>> /var/log/report_daemon/report_daemon.log", REPORT_DIR, backup_dir);
        execl("/bin/sh", "sh", "-c", command, (char *) NULL);

        // If execl fails
        log_message(CLOG_ERROR, "Failed to execute backup command: %s", strerror(errno));
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        close(pipe_fd[1]); // Close write end
        
        // Reading output from child process
        while ((bytes_read = read(pipe_fd[0], buffer, BUFFER_SIZE - 1)) > 0) {
            buffer[bytes_read] = '\0';
            log_message(CLOG_INFO, "%s", buffer);
        }
        
        close(pipe_fd[0]);
        
        // Waiting for child to complete
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                log_message(CLOG_INFO, "Backup completed successfully to %s", backup_dir);
            } else {
                log_message(CLOG_ERROR, "Backup failed with status %d", WEXITSTATUS(status));
            }
        } else {
            log_message(CLOG_ERROR, "Backup process terminated abnormally");
        }
        
        // Unlock directories after backup
        unlock_directories();
    }
}