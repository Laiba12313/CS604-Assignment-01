/*
 * Operating Systems (CS604) - Assignment 01
 * Student ID: BC240212364
 * Program: Mini Task Processing System using fork() and two pipes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#define TASK_COUNT 3
#define BUFFER_SIZE 1024

typedef struct {
    long long total_time;
    int longest_task;
    int maximum_time;
} TaskResult;

/* Write all requested bytes, even if write() transfers fewer bytes. */
int write_all(int fd, const void *buffer, size_t size) {
    const char *ptr = (const char *)buffer;
    size_t sent = 0;

    while (sent < size) {
        ssize_t n = write(fd, ptr + sent, size - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* Read all requested bytes, even if read() transfers fewer bytes. */
int read_all(int fd, void *buffer, size_t size) {
    char *ptr = (char *)buffer;
    size_t received = 0;

    while (received < size) {
        ssize_t n = read(fd, ptr + received, size - received);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        received += (size_t)n;
    }
    return 0;
}

int main(void) {
    char student_id[64];
    char report_filename[100];
    int task_times[TASK_COUNT];
    int parent_to_child[2];
    int child_to_parent[2];
    pid_t pid;
    TaskResult result;
    char report[BUFFER_SIZE];
    int report_length;
    int report_fd;
    int status;

    printf("Enter Student ID: ");
    if (scanf("%63s", student_id) != 1) {
        fprintf(stderr, "Error: Invalid Student ID input.\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < TASK_COUNT; i++) {
        printf("Enter Task %d Execution Time: ", i + 1);
        if (scanf("%d", &task_times[i]) != 1 || task_times[i] <= 0) {
            fprintf(stderr, "Error: Task times must be positive integers.\n");
            return EXIT_FAILURE;
        }
    }

    /* Create two pipes for communication in both directions. */
    if (pipe(parent_to_child) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }
    if (pipe(child_to_parent) == -1) {
        perror("pipe");
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        return EXIT_FAILURE;
    }

    pid = fork();

    if (pid < 0) {
        perror("fork");
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* Child process: receive task times from the parent. */
        close(parent_to_child[1]);
        close(child_to_parent[0]);

        int received_times[TASK_COUNT];
        if (read_all(parent_to_child[0], received_times,
                     sizeof(received_times)) == -1) {
            perror("child read");
            close(parent_to_child[0]);
            close(child_to_parent[1]);
            _exit(EXIT_FAILURE);
        }

        close(parent_to_child[0]);

        result.total_time = 0;
        result.longest_task = 1;
        result.maximum_time = received_times[0];

        for (int i = 0; i < TASK_COUNT; i++) {
            result.total_time += received_times[i];

            /* Strictly greater preserves the smaller task number on a tie. */
            if (received_times[i] > result.maximum_time) {
                result.maximum_time = received_times[i];
                result.longest_task = i + 1;
            }
        }

        /* Send calculated results back to the parent through pipe 2. */
        if (write_all(child_to_parent[1], &result, sizeof(result)) == -1) {
            perror("child write");
            close(child_to_parent[1]);
            _exit(EXIT_FAILURE);
        }

        close(child_to_parent[1]);
        _exit(EXIT_SUCCESS);
    }

    /* Parent process: send task times and receive the child's result. */
    close(parent_to_child[0]);
    close(child_to_parent[1]);

    if (write_all(parent_to_child[1], task_times, sizeof(task_times)) == -1) {
        perror("parent write");
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        waitpid(pid, NULL, 0);
        return EXIT_FAILURE;
    }
    close(parent_to_child[1]);

    if (read_all(child_to_parent[0], &result, sizeof(result)) == -1) {
        perror("parent read");
        close(child_to_parent[0]);
        waitpid(pid, NULL, 0);
        return EXIT_FAILURE;
    }
    close(child_to_parent[0]);

    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "Error: Child process did not finish successfully.\n");
        return EXIT_FAILURE;
    }

    /* Prepare one report for both terminal display and the text file. */
    report_length = snprintf(
        report, sizeof(report),
        "\n--- Task Processing Report ---\n"
        "Student ID: %s\n"
        "Task 1: %d units\n"
        "Task 2: %d units\n"
        "Task 3: %d units\n"
        "Total Execution Time: %lld units\n"
        "Longest Task: Task %d\n"
        "Maximum Execution Time: %d units\n",
        student_id,
        task_times[0], task_times[1], task_times[2],
        result.total_time, result.longest_task, result.maximum_time
    );

    if (report_length < 0 || report_length >= (int)sizeof(report)) {
        fprintf(stderr, "Error: Could not prepare the report.\n");
        return EXIT_FAILURE;
    }

    printf("%s", report);

    if (snprintf(report_filename, sizeof(report_filename),
                 "%s_report.txt", student_id) >= (int)sizeof(report_filename)) {
        fprintf(stderr, "Error: Student ID is too long for the report filename.\n");
        return EXIT_FAILURE;
    }

    /* Create or overwrite the report file using Linux file operations. */
    report_fd = open(report_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (report_fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (write_all(report_fd, report, (size_t)report_length) == -1) {
        perror("write report");
        close(report_fd);
        return EXIT_FAILURE;
    }

    if (close(report_fd) == -1) {
        perror("close report");
        return EXIT_FAILURE;
    }

    printf("Report saved to: %s\n", report_filename);
    return EXIT_SUCCESS;
}
