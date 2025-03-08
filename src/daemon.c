/* daemon.c - Implementation of daemon functionality */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/file.h>
#include <dirent.h>
#include <pthread.h>

#include "../include/config.h"
#include "../include/daemon.h"
#include "../include/file_ops.h"
#include "../include/logging.h"
#include <linux/limits.h>

#ifndef DT_REG
#define DT_REG 8  // Value for regular files
#endif

/* Global variables */
volatile sig_atomic_t running = 1;
volatile sig_atomic_t force_backup = 0;
pthread_mutex_t dir_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Signal handler for termination signals */
void handle_signal(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            log_message(CLOG_INFO, "Received termination signal, shutting down...");
            running = 0;
            break;
        case SIGUSR1:
            log_message(CLOG_INFO, "Received manual backup signal");
            force_backup = 1;
            break;
    }
}

/* Check if daemon is already running */
int check_daemon_running(const char *pid_file) {
    FILE *fp;
    pid_t pid;
    
    if ((fp = fopen(pid_file, "r")) != NULL) {
        if (fscanf(fp, "%d", &pid) == 1) {
            fclose(fp);
            if (kill(pid, 0) == 0) {
                log_message(CLOG_WARNING, "Daemon already running with PID %d", pid);
                return 1;
            }
        }
        fclose(fp);
    }
    
    return 0;
}

/* Write PID to file */
int write_pid_file(const char *pid_file) {
    FILE *fp;
    
    if ((fp = fopen(pid_file, "w")) == NULL) {
        log_message(CLOG_ERROR, "Failed to open PID file %s: %s", pid_file, strerror(errno));
        return -1;
    }
    
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    
    return 0;
}

/* Check uploaded XML reports and log changes */
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
    
    strftime(today_date, sizeof(today_date), "%Y%m%d", time_info);
    
    dir = opendir(UPLOAD_DIR);
    if (dir == NULL) {
        log_message(CLOG_ERROR, "Failed to open upload directory: %s", strerror(errno));
        return;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".xml") != NULL) {
            if (strstr(entry->d_name, "warehouse") && strstr(entry->d_name, today_date))
                warehouse_found = 1;
            else if (strstr(entry->d_name, "manufacturing") && strstr(entry->d_name, today_date))
                manufacturing_found = 1;
            else if (strstr(entry->d_name, "sales") && strstr(entry->d_name, today_date))
                sales_found = 1;
            else if (strstr(entry->d_name, "distribution") && strstr(entry->d_name, today_date))
                distribution_found = 1;
        }
    }
    
    closedir(dir);
    
    // Log missing reports
    if (!warehouse_found)
        log_message(CLOG_WARNING, "Missing warehouse report for %s", today_date);
    if (!manufacturing_found)
        log_message(CLOG_WARNING, "Missing manufacturing report for %s", today_date);
    if (!sales_found)
        log_message(CLOG_WARNING, "Missing sales report for %s", today_date);
    if (!distribution_found)
        log_message(CLOG_WARNING, "Missing distribution report for %s", today_date);
}

/* Lock directories before backup/transfer operations */
int lock_directories() {
    int ret = pthread_mutex_lock(&dir_mutex);
    if (ret != 0) {
        log_message(CLOG_ERROR, "Failed to lock directories: %s", strerror(ret));
        return -1;
    }
    
    // Change permissions to read-only
    if (chmod(UPLOAD_DIR, S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        log_message(CLOG_ERROR, "Failed to change upload directory permissions: %s", strerror(errno));
        pthread_mutex_unlock(&dir_mutex);
        return -1;
    }
    
    if (chmod(REPORT_DIR, S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        log_message(CLOG_ERROR, "Failed to change report directory permissions: %s", strerror(errno));
        chmod(UPLOAD_DIR, UPLOAD_DIR_PERMS); // Restore upload directory permissions
        pthread_mutex_unlock(&dir_mutex);
        return -1;
    }
    
    log_message(CLOG_INFO, "Directories locked for backup/transfer");
    return 0;
}

/* Unlock directories after backup/transfer operations */
int unlock_directories() {
    // Ensure /var/reports/ allows access to subdirectories (read & execute, but no modification)
    if (chmod("/var/reports", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        log_message(CLOG_ERROR, "Failed to set /var/reports permissions: %s", strerror(errno));
        return -1;
    }

    // Restore upload directory to be writable (Owner: Read, Write, Execute | Group: Read, Write, Execute | Others: Read)
    if (chmod(UPLOAD_DIR, S_IRWXU | S_IRWXG | S_IROTH) != 0) {
        log_message(CLOG_ERROR, "Failed to restore upload directory permissions: %s", strerror(errno));
        return -1;
    }

    // Ensure dashboard directory is read-only (Users can enter but cannot modify files)
    if (chmod(REPORT_DIR, S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
        log_message(CLOG_ERROR, "Failed to set dashboard directory as read-only: %s", strerror(errno));
        return -1;
    }

    // Ensure all files in dashboard are read-only (644)
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




/* Transfer XML reports from upload to report directory */
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
        
        // Execute the transfer operation
        execl("/bin/sh", "sh", "-c", "find " UPLOAD_DIR " -name \"*.xml\" -exec cp {} " REPORT_DIR " \\; -exec echo \"Transferred: {}\" \\;", NULL);
        
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
        
        // Wait for child to complete
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
        
        // Unlock directories after transfer
        unlock_directories();
    }
}

/* Backup report directory */
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
    
    // Create timestamp for backup directory
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", time_info);
    snprintf(backup_dir, PATH_MAX, "%s/%s", BACKUP_DIR, timestamp);
    
    // Create backup directory
    if (mkdir(backup_dir, 0755) != 0) {
        log_message(CLOG_ERROR, "Failed to create backup directory %s: %s", backup_dir, strerror(errno));
        return;
    }
    
    // Create a pipe for IPC
    if (pipe(pipe_fd) == -1) {
        log_message(CLOG_ERROR, "Failed to create pipe: %s", strerror(errno));
        return;
    }
    
    // Lock directories before backup
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
        
        // Redirect stdout to pipe
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        
        // Execute the backup operation
        char command[PATH_MAX];
        snprintf(command, sizeof(command), "cp -r %s/*.xml %s/ 2>> /var/log/report_daemon/report_daemon.log", REPORT_DIR, backup_dir);
        execl("/bin/sh", "sh", "-c", command, (char *) NULL);

        // If execl fails
        log_message(CLOG_ERROR, "Failed to execute backup command: %s", strerror(errno));
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
        
        // Wait for child to complete
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

/* Initialize and start the daemon */
int start_daemon(const char *pid_file) {
    pid_t pid, sid;
    
    // Check if daemon is already running
    if (check_daemon_running(pid_file)) {
        return -1;
    }
    
    // Fork off the parent process
    pid = fork();
    if (pid < 0) {
        log_message(CLOG_ERROR, "Failed to fork daemon: %s", strerror(errno));
        return -1;
    }
    
    // Exit the parent process
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // Create a new SID for the child process
    sid = setsid();
    if (sid < 0) {
        log_message(CLOG_ERROR, "Failed to create new session: %s", strerror(errno));
        return -1;
    }
    
    // Change the current working directory
    if (chdir("/") < 0) {
        log_message(CLOG_ERROR, "Failed to change directory: %s", strerror(errno));
        return -1;
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect standard file descriptors to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        log_message(CLOG_ERROR, "Failed to open /dev/null: %s", strerror(errno));
        return -1;
    }
    
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    
    if (fd > STDERR_FILENO) {
        close(fd);
    }
    
    // Set up signal handlers
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGUSR1, handle_signal);
    
    // Write PID file
    if (write_pid_file(pid_file) != 0) {
        return -1;
    }
    
    // Initialize logging
    init_logging();
    
    log_message(CLOG_INFO, "Daemon started successfully");
    
    // Create directories if they don't exist
    if (create_directory_if_not_exists(UPLOAD_DIR) != 0 ||
        create_directory_if_not_exists(REPORT_DIR) != 0 ||
        create_directory_if_not_exists(BACKUP_DIR) != 0) {
        log_message(CLOG_ERROR, "Failed to create required directories");
        return -1;
    }
    
    // Set directory permissions
    if (chmod(UPLOAD_DIR, UPLOAD_DIR_PERMS) != 0 ||
        chmod(REPORT_DIR, REPORT_DIR_PERMS) != 0) {
        log_message(CLOG_ERROR, "Failed to set directory permissions");
        return -1;
    }
    
    return 0;
}

/* Run the daemon main loop */
void run_daemon() {
    time_t last_check_time = 0;
    time_t now;
    struct tm *time_info;
    int is_transfer_time = 0;
    
    // Main daemon loop
    while (running) {
        now = time(NULL);
        time_info = localtime(&now);
        
        // Check if it's 1AM (transfer time)
        is_transfer_time = (time_info->tm_hour == 1 && time_info->tm_min == 0 && time_info->tm_sec < CHECK_INTERVAL);
        
        // Check for manual backup signal
        if (force_backup) {
            log_message(CLOG_INFO, "Manual backup and transfer requested");
            backup_reports();
            transfer_reports();
            force_backup = 0;
        }
        // Check for scheduled backup/transfer time
        else if (is_transfer_time) {
            log_message(CLOG_INFO, "Scheduled backup and transfer at 1AM");
            
            // Check for missing reports
            check_missing_reports();
            
            // Perform backup and transfer
            backup_reports();
            transfer_reports();
        }
        
        // Regular check for file changes
        if (now - last_check_time >= CHECK_INTERVAL) {
            check_uploads();
            last_check_time = now;
        }
        
        // Sleep to reduce CPU usage
        sleep(1);
    }
    
    log_message(CLOG_INFO, "Daemon shutting down");
}

/* Stop the daemon */
void stop_daemon(const char *pid_file) {
    FILE *fp;
    pid_t pid;
    
    if ((fp = fopen(pid_file, "r")) == NULL) {
        log_message(CLOG_ERROR, "Failed to open PID file %s: %s", pid_file, strerror(errno));
        return;
    }
    
    if (fscanf(fp, "%d", &pid) != 1) {
        log_message(CLOG_ERROR, "Failed to read PID from file %s", pid_file);
        fclose(fp);
        return;
    }
    
    fclose(fp);
    
    if (kill(pid, SIGTERM) != 0) {
        log_message(CLOG_ERROR, "Failed to terminate daemon with PID %d: %s", pid, strerror(errno));
        return;
    }
    
    log_message(CLOG_INFO, "Daemon with PID %d terminated", pid);
    
    // Remove PID file
    if (unlink(pid_file) != 0) {
        log_message(CLOG_ERROR, "Failed to remove PID file %s: %s", pid_file, strerror(errno));
    }
}