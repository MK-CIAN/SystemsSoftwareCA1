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

// Assigning Global variables
volatile sig_atomic_t running = 1;
volatile sig_atomic_t force_backup = 0;

// Signal handler for termination signals
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

int check_daemon_running(const char *pid_file) {
    FILE *fp;
    pid_t pid;
    
    fp = fopen(pid_file, "r");
    if (fp == NULL) {
        return 0;  // File does not exist, so daemon is not running
    }

    int read_pid = fscanf(fp, "%d", &pid);
    fclose(fp);  // Ensure `fclose(fp)` is always called exactly once since I had an issue with it being called twice

    if (read_pid == 1 && kill(pid, 0) == 0) {
        log_message(CLOG_WARNING, "Daemon already running with PID %d", pid);
        return 1;
    }

    return 0;
}


// Write PID to file
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

// Initializing and starting the daemon

int start_daemon(const char *pid_file) {
    pid_t pid, sid;

    // Checking if daemon is already running
    if (check_daemon_running(pid_file)) {
        return -1;
    }

    // Forking off the parent process
    pid = fork();
    if (pid < 0) {

        log_message(CLOG_ERROR, "Failed to fork daemon: %s", strerror(errno));

        return -1;

    }



    // Exiting the parent process

    if (pid > 0) {

        exit(EXIT_SUCCESS);

    }



    // Creating a new SID for the child process

    sid = setsid();

    if (sid < 0) {

        log_message(CLOG_ERROR, "Failed to create new session: %s", strerror(errno));

        return -1;

    }



    // Changing the current working directory

    if (chdir("/") < 0) {

        log_message(CLOG_ERROR, "Failed to change directory: %s", strerror(errno));

        return -1;

    }



    // Closing standard file descriptors

    close(STDIN_FILENO);

    close(STDOUT_FILENO);

    close(STDERR_FILENO);



    // Redirecting standard file descriptors to /dev/null

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



    // Setting up the signal handlers

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

    // Ensure directories exist
    if (create_directory_if_not_exists(UPLOAD_DIR) != 0 ||
        create_directory_if_not_exists(REPORT_DIR) != 0 ||
        create_directory_if_not_exists(BACKUP_DIR) != 0) {
        log_message(CLOG_ERROR, "Failed to create required directories");
        return -1;
    }

    //Ensure correct permissions at startup
    if (chmod(UPLOAD_DIR, 0777) != 0) { // Fully open for all users
        log_message(CLOG_ERROR, "Failed to set upload directory permissions at startup: %s", strerror(errno));
    }

    if (chmod(REPORT_DIR, 0755) != 0) { // Readable by all, modifiable only by root
        log_message(CLOG_ERROR, "Failed to set dashboard directory permissions at startup: %s", strerror(errno));
    }
    return 0;
}

// Running the daemon in the main loop
void run_daemon() {
    time_t last_check_time = 0;
    time_t now;
    struct tm *time_info;
    int last_backup_day = -1;  // Track the last day a backup was performed
    
    // Main daemon loop
    while (running) {
        now = time(NULL);
        time_info = localtime(&now);

        // Check if it's exactly 1:00 AM and hasn't run today
        if (time_info->tm_hour == 1 && time_info->tm_min == 0 && time_info->tm_mday != last_backup_day) {
            last_backup_day = time_info->tm_mday;  // Mark backup as done for today
            log_message(CLOG_INFO, "Scheduled backup and transfer at 1AM");
            
            // Check for missing reports
            check_missing_reports();
            
            // Perform backup and transfer
            backup_reports();
            transfer_reports();
        }

        // Check for manual backup signal
        if (force_backup) {
            log_message(CLOG_INFO, "Manual backup and transfer requested");
            backup_reports();
            transfer_reports();
            force_backup = 0;
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

// Stopping the daemon
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