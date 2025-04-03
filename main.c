#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define FIFO1 "fifo1"
#define FIFO2 "fifo2"
#define LOGFILE "logs.txt"
#define TIMEOUT 30
#define MAX_LOG_MSG 256

int counter = 0;
int result = 0;  // Initial result value as per requirements
volatile sig_atomic_t running = 1;

void log_message(const char *message) {
    FILE *log = fopen(LOGFILE, "a");
    if (log) {
        time_t now = time(NULL);
        char timestamp[26];
        ctime_r(&now, timestamp);
        timestamp[24] = '\0';  // Remove newline
        fprintf(log, "[%s] [PID:%d] %s\n", timestamp, getpid(), message);
        fflush(log);
        fclose(log);
    }
}

void signal_handler(int signo) {
    char msg[MAX_LOG_MSG];
    if (signo == SIGCHLD) {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            counter += 2;  // Increment counter by 2 as per requirements
            
            if (WIFEXITED(status)) {
                snprintf(msg, MAX_LOG_MSG, "Child process %d exited normally with status %d", 
                        pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                snprintf(msg, MAX_LOG_MSG, "Child process %d terminated by signal %d", 
                        pid, WTERMSIG(status));
            }
            log_message(msg);
            printf("%s. Counter: %d\n", msg, counter);
            fflush(stdout);
        }
    }
}

void handle_daemon_signals(int signo) {
    switch(signo) {
        case SIGUSR1:
            log_message("Daemon received SIGUSR1");
            break;
        case SIGHUP:
            log_message("Daemon received SIGHUP - Reconfiguring");
            // Implement reconfiguration logic here
            break;
        case SIGTERM:
            log_message("Daemon received SIGTERM - Shutting down gracefully");
            running = 0;
            break;
    }
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void daemon_process(pid_t child1, pid_t child2) {
    log_message("Daemon started - monitoring child processes");
    
    time_t start_time = time(NULL);
    
    while (running) {
        sleep(1);
        
        // Check for timeout
        if (time(NULL) - start_time > TIMEOUT) {
            log_message("Timeout reached - terminating inactive processes");
            kill(child1, SIGTERM);
            kill(child2, SIGTERM);
            break;
        }

        // Check child processes
        if (waitpid(child1, NULL, WNOHANG) > 0 && waitpid(child2, NULL, WNOHANG) > 0) {
            log_message("All child processes completed - daemon terminating");
            break;
        }
    }
    
    log_message("Daemon stopping");
    exit(0);
}

void daemonize() {
    log_message("Starting daemonization process");

    if (setsid() < 0) {
        log_message("Failed to create new session");
        exit(1);
    }

    umask(0);
    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open(LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    open(LOGFILE, O_WRONLY | O_CREAT | O_APPEND, 0666);

    log_message("Daemonization completed");
}

void child1_process() {
    char msg[MAX_LOG_MSG];
    log_message("Child process 1 started");

    int fd = open(FIFO1, O_RDONLY);
    if (fd < 0) {
        snprintf(msg, MAX_LOG_MSG, "Child Process 1: Failed to open FIFO1: %s", strerror(errno));
        log_message(msg);
        exit(1);
    }

    int num1, num2;
    if (read(fd, &num1, sizeof(int)) <= 0 || read(fd, &num2, sizeof(int)) <= 0) {
        snprintf(msg, MAX_LOG_MSG, "Child Process 1: Read error: %s", strerror(errno));
        log_message(msg);
        close(fd);
        exit(1);
    }
    close(fd);

    snprintf(msg, MAX_LOG_MSG, "Child Process 1: Read numbers %d and %d", num1, num2);
    log_message(msg);

    result = (num1 > num2) ? num1 : num2;

    fd = open(FIFO2, O_WRONLY);
    if (fd < 0) {
        snprintf(msg, MAX_LOG_MSG, "Child Process 1: Failed to open FIFO2: %s", strerror(errno));
        log_message(msg);
        exit(1);
    }

    write(fd, &result, sizeof(int));
    close(fd);

    snprintf(msg, MAX_LOG_MSG, "Child Process 1: Wrote larger number %d to FIFO2", result);
    log_message(msg);

    sleep(10);
    log_message("Child Process 1: Exiting normally");
    exit(0);
}

void child2_process() {
    char msg[MAX_LOG_MSG];
    log_message("Child process 2 started");

    int fd = open(FIFO2, O_RDONLY);
    if (fd < 0) {
        snprintf(msg, MAX_LOG_MSG, "Child Processs 2: Failed to open FIFO2: %s", strerror(errno));
        log_message(msg);
        exit(1);
    }

    int larger_num;
    if (read(fd, &larger_num, sizeof(int)) <= 0) {
        snprintf(msg, MAX_LOG_MSG, "Child Processs 2: Read error: %s", strerror(errno));
        log_message(msg);
        close(fd);
        exit(1);
    }
    close(fd);

    snprintf(msg, MAX_LOG_MSG, "Child Processs 2: Larger number is: %d", larger_num);
    log_message(msg);
    printf("Larger number: %d\n", larger_num);
    fflush(stdout);

    sleep(10);
    log_message("Child Processs 2: Exiting normally");
    exit(0);
}

void parent_process_loop() {
    while (counter < 4) {  // Counter should reach 4 (2 per child)
        printf("Proceeding...\n");
        fflush(stdout);
        log_message("Parent: Proceeding...");
        sleep(2);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <int1> <int2>\n", argv[0]);
        exit(1);
    }

    // Initialize logging
    log_message("Program started");

    int num1 = atoi(argv[1]);
    int num2 = atoi(argv[2]);

    // Create FIFOs
    unlink(FIFO1);
    unlink(FIFO2);
    if (mkfifo(FIFO1, 0666) < 0 || mkfifo(FIFO2, 0666) < 0) {
        log_message("Failed to create FIFOs");
        exit(1);
    }
    log_message("FIFOs created successfully");

    // Set up signal handlers
    signal(SIGCHLD, signal_handler);
    signal(SIGUSR1, handle_daemon_signals);
    signal(SIGHUP, handle_daemon_signals);
    signal(SIGTERM, handle_daemon_signals);

    // Create child processes
    pid_t child1_pid = fork();
    if (child1_pid == 0) {
        child1_process();
    }

    pid_t child2_pid = fork();
    if (child2_pid == 0) {
        child2_process();
    }

    // Parent writes to FIFO1
    int fd = open(FIFO1, O_WRONLY);
    if (fd < 0) {
        log_message("Parent: Failed to open FIFO1");
        exit(1);
    }
    write(fd, &num1, sizeof(int));
    write(fd, &num2, sizeof(int));
    close(fd);
    
    char msg[MAX_LOG_MSG];
    snprintf(msg, MAX_LOG_MSG, "Parent: Wrote numbers %d and %d to FIFO1", num1, num2);
    log_message(msg);

    // Create daemon process
    pid_t daemon_pid = fork();
    if (daemon_pid == 0) {
        daemonize();
        daemon_process(child1_pid, child2_pid);
    }

    // Parent process loop
    parent_process_loop();

    // Cleanup
    printf("All child processes terminated. Exiting Program...\n");
    log_message("Parent: Cleaning up and exiting");
    unlink(FIFO1);
    unlink(FIFO2);

    return 0;
}