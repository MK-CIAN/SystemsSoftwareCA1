#!/bin/bash
### BEGIN INIT INFO
# Provides:          report_daemon
# Required-Start:    $remote_fs $syslog
# Required-Stop:     $remote_fs $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Report Management Daemon
# Description:       Daemon to manage report uploads, transfers, and backups
### END INIT INFO

# Path to the daemon executable
DAEMON_PATH="/usr/sbin/report_daemon"

# Daemon name
NAME="report_daemon"

# Path to PID file
PIDFILE="/var/run/report_daemon.pid"

# Log file
LOGFILE="/var/log/report_daemon/report_daemon.log"

# Check if daemon executable exists
if [ ! -x "$DAEMON_PATH" ]; then
    echo "Daemon executable $DAEMON_PATH not found or not executable!"
    exit 1
fi

# Function to check if daemon is running
check_running() {
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if [ -d "/proc/$PID" ]; then
            return 0
        else
            # PID file exists but process is not running
            rm -f "$PIDFILE"
        fi
    fi
    return 1
}

# Start the daemon
start() {
    if check_running; then
        echo "$NAME is already running."
        return 0
    fi
    
    echo "Starting $NAME..."
    
    # Create log directory if it doesn't exist
    mkdir -p "$(dirname "$LOGFILE")"
    
    # Start the daemon
    $DAEMON_PATH start
    
    # Check if daemon started successfully
    sleep 1
    if check_running; then
        echo "$NAME started successfully."
        return 0
    else
        echo "Failed to start $NAME."
        return 1
    fi
}

# Stop the daemon
stop() {
    if ! check_running; then
        echo "$NAME is not running."
        return 0
    fi
    
    echo "Stopping $NAME..."
    
    # Stop the daemon
    $DAEMON_PATH stop
    
    # Wait for daemon to stop
    local count=0
    while check_running && [ $count -lt 10 ]; do
        sleep 1
        count=$((count + 1))
    done
    
    if check_running; then
        echo "Failed to stop $NAME gracefully, using force..."
        PID=$(cat "$PIDFILE")
        kill -9 "$PID"
        rm -f "$PIDFILE"
    fi
    
    echo "$NAME stopped."
    return 0
}

# Restart the daemon
restart() {
    stop
    sleep 1
    start
}

# Get status of the daemon
status() {
    if check_running; then
        PID=$(cat "$PIDFILE")
        echo "$NAME is running with PID $PID."
        return 0
    else
        echo "$NAME is not running."
        return 1
    fi
}

# Force immediate backup and transfer
backup() {
    if ! check_running; then
        echo "$NAME is not running."
        return 1
    fi
    
    echo "Signaling $NAME to perform immediate backup and transfer..."
    $DAEMON_PATH backup
    return 0
}

# Main case statement
case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        restart
        ;;
    status)
        status
        ;;
    backup)
        backup
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status|backup}"
        exit 1
        ;;
esac

exit $?