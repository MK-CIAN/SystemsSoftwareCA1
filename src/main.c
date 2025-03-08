/* main.c - Main program for report management daemon */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "../include/config.h"
#include "../include/daemon.h"
#include "../include/logging.h"
#include <sys/types.h>
#include <errno.h>

void print_usage(const char *program_name) {
    printf("Usage: %s [start|stop|status|backup]\n", program_name);
    printf("  start   - Start the daemon\n");
    printf("  stop    - Stop the daemon\n");
    printf("  status  - Check if the daemon is running\n");
    printf("  backup  - Signal running daemon to perform backup\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    // Initialize logging
    if (init_logging() != 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return EXIT_FAILURE;
    }
    
    // Process commands
    if (strcmp(argv[1], "start") == 0) {
        // Start daemon
        log_message(CLOG_INFO, "Starting daemon...");
        if (start_daemon(PID_FILE) != 0) {
            log_message(CLOG_ERROR, "Failed to start daemon");
            return EXIT_FAILURE;
        }
        
        // Run daemon main loop
        run_daemon();
        
    } else if (strcmp(argv[1], "stop") == 0) {
        // Stop daemon
        log_message(CLOG_INFO, "Stopping daemon...");
        stop_daemon(PID_FILE);
        
    } else if (strcmp(argv[1], "status") == 0) {
        // Check daemon status
        if (check_daemon_running(PID_FILE)) {
            printf("Daemon is running\n");
        } else {
            printf("Daemon is not running\n");
        }
        
    } else if (strcmp(argv[1], "backup") == 0) {
        // Signal daemon to perform backup
        FILE *fp;
        pid_t pid;
        
        if ((fp = fopen(PID_FILE, "r")) == NULL) {
            log_message(CLOG_ERROR, "Failed to open PID file %s: Daemon not running?", PID_FILE);
            return EXIT_FAILURE;
        }
        
        if (fscanf(fp, "%d", &pid) != 1) {
            log_message(CLOG_ERROR, "Failed to read PID from file %s", PID_FILE);
            fclose(fp);
            return EXIT_FAILURE;
        }
        
        fclose(fp);
        
        if (kill(pid, SIGUSR1) != 0) {
            log_message(CLOG_ERROR, "Failed to send signal to daemon: %s", strerror(errno));
            return EXIT_FAILURE;
        }
        
        printf("Signal sent to daemon for immediate backup and transfer\n");
        
    } else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    // Clean up logging
    cleanup_logging();
    
    return EXIT_SUCCESS;
}