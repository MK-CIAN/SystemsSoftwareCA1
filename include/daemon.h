/* daemon.h - Functions for daemon management */

#ifndef DAEMON_H
#define DAEMON_H

#include <signal.h>

/* Check if daemon is already running */
int check_daemon_running(const char *pid_file);

/* Write PID to file */
int write_pid_file(const char *pid_file);

/* Initialize and start the daemon */
int start_daemon(const char *pid_file);

/* Run the daemon main loop */
void run_daemon();

/* Stop the daemon */
void stop_daemon(const char *pid_file);

/* Signal handler */
void handle_signal(int sig);

#endif /* DAEMON_H */