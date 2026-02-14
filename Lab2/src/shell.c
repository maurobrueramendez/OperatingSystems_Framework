#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#include "circularBuffer.h"
#include "splitCommand.h"

#define READ_CHUNK 1024     // read per system call
#define LINE_MAX   4096     // maximum command line len

// remove trailing newline or carriage return characters from a string
// (lines read from input usually end with newline)
static void strip_newline(char *s) {
    if (!s) return;

    size_t n = strlen(s);

    // remove newline characters from end of string
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

// clean up finished background children to avoid zombie processes
// (concurrent commands do not block the shell)
static void reap_background_children(void) {
    int status = 0;

    // repeatedly try to reap finished children
    while (1) {
        pid_t p = waitpid(-1, &status, WNOHANG);

        // a child was reaped successfully
        if (p > 0) continue;

        // no finished children right now
        if (p == 0) break;

        // retry if interrupted
        if (errno == EINTR) continue;

        // no child processes exist
        if (errno == ECHILD) break;

        break;
    }
}

// read one full line from stdin using the circular buffer
// hides partial reads and buffering complexity
// returns 1 if a line is read, 0 on eof, -1 on error
static int read_line_cb(CircularBuffer *cb, int fd, char *out, size_t out_cap) {
    // static variable remembers eof state across calls
    static int reachedEOF = 0;

    // temporary buffer used when reading from stdin
    unsigned char tmp[READ_CHUNK];

    while (1) {

        // check whether a full line is already stored in the buffer
        int next_sz = buffer_size_next_element(cb, '\n', reachedEOF);

        // if a full line or final element is available
        if (next_sz != -1) {

            // check output buffer capacity
            if ((size_t)next_sz >= out_cap) {

                // discard the oversized line from circular buffer
                for (int i = 0; i < next_sz; i++)
                    buffer_pop(cb);

                errno = ENOMEM;
                return -1;
            }

            // copy line bytes from circular buffer
            for (int i = 0; i < next_sz; i++) {
                out[i] = (char)buffer_pop(cb);
            }

            // terminate string
            out[next_sz] = '\0';

            // remove trailing newline
            strip_newline(out);

            return 1;
        }

        // otherwise we need to read more data from stdin
        ssize_t r = read(fd, tmp, sizeof(tmp));

        // handle read errors
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        // if end of file is reached
        if (r == 0) {
            reachedEOF = 1;

            // if buffer empty, done
            if (buffer_used_bytes(cb) == 0)
                return 0;

            // otherwise try extracting final line
            continue;
        }

        // push all read bytes into circular buffer
        for (ssize_t i = 0; i < r; i++) {

            // ensure buffer still has space
            if (buffer_free_bytes(cb) <= 0) {
                errno = ENOMEM;
                return -1;
            }

            buffer_push(cb, tmp[i]);
        }
    }
}

// execute command in child process, terminate if exec fails
static void exec_child_or_die(char **argv) {

    // empty command, exit 
    if (!argv || !argv[0]) _exit(0);

    // replace child process with requested program
    execvp(argv[0], argv);

    // if exec returns then an error occurred
    perror("execvp");

    // free argv allocated by split_command
    free(argv);

    _exit(1);
}

// execute a single command
// wait_for_child controls foreground or background execution
static int run_single(char *cmdline, int wait_for_child) {

    // split command line into argv format
    char **argv = split_command(cmdline);
    if (!argv) {
        perror("split_command");
        return -1;
    }

    // create child process
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        free(argv);
        return -1;
    }

    // child process executes command
    if (pid == 0) {
        exec_child_or_die(argv);
    }

    // parent optionally waits for child
    if (wait_for_child) {
        int status = 0;

        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            free(argv);
            return -1;
        }
    }

    // free argument array in parent
    free(argv);
    return 0;
}

// execute two commands connected by a pipe
static int run_piped(char *cmd1, char *cmd2) {

    int fds[2];

    // create pipe, fds[0] read end, fds[1] write end
    if (pipe(fds) < 0) {
        perror("pipe");
        return -1;
    }

    // prepare first command
    char **argv1 = split_command(cmd1);
    if (!argv1) {
        perror("split_command");
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    // create first child
    pid_t p1 = fork();
    if (p1 < 0) {
        perror("fork");
        free(argv1);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    // first child writes into pipe
    if (p1 == 0) {

        // redirect stdout to pipe write end
        dup2(fds[1], STDOUT_FILENO);

        close(fds[0]);
        close(fds[1]);

        exec_child_or_die(argv1);
    }

    // prepare second command
    char **argv2 = split_command(cmd2);
    if (!argv2) {
        perror("split_command");
        free(argv1);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    // create second child
    pid_t p2 = fork();
    if (p2 < 0) {
        perror("fork");
        free(argv1);
        free(argv2);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    // second child reads from pipe
    if (p2 == 0) {

        // redirect stdin from pipe read end
        dup2(fds[0], STDIN_FILENO);

        close(fds[1]);
        close(fds[0]);

        exec_child_or_die(argv2);
    }

    // parent closes both pipe ends
    close(fds[0]);
    close(fds[1]);

    // free argument arrays
    free(argv1);
    free(argv2);

    // wait for both children
    int status = 0;
    waitpid(p1, &status, 0);
    waitpid(p2, &status, 0);

    return 0;
}

int main(void) {

    // initialise circular buffer for input processing
    CircularBuffer cb;
    if (buffer_init(&cb, 8192) != 0) {
        fprintf(stderr, "failed to allocate circular buffer\n");
        return 1;
    }

    // buffers storing mode and command lines
    char mode[LINE_MAX];
    char line1[LINE_MAX];
    char line2[LINE_MAX];

    while (1) {

        // clean finished background processes
        reap_background_children();

        // read execution mode line
        int r = read_line_cb(&cb, STDIN_FILENO, mode, sizeof(mode));
        if (r == 0) break;
        if (r < 0) {
            perror("read");
            break;
        }

        strip_newline(mode);

        // exit instruction terminates shell
        if (strcmp(mode, "EXIT") == 0)
            break;

        // single foreground command
        if (strcmp(mode, "SINGLE") == 0) {

            if (read_line_cb(&cb, STDIN_FILENO, line1, sizeof(line1)) <= 0)
                break;

            run_single(line1, 1);
        }

        // background command
        else if (strcmp(mode, "CONCURRENT") == 0) {

            if (read_line_cb(&cb, STDIN_FILENO, line1, sizeof(line1)) <= 0)
                break;

            run_single(line1, 0);
        }

        // piped commands
        else if (strcmp(mode, "PIPED") == 0 ||
                 strcmp(mode, "PIPE") == 0) {

            if (read_line_cb(&cb, STDIN_FILENO, line1, sizeof(line1)) <= 0)
                break;

            if (read_line_cb(&cb, STDIN_FILENO, line2, sizeof(line2)) <= 0)
                break;

            run_piped(line1, line2);
        }

        // unknown instruction
        else {
            fprintf(stderr, "unknown execution mode %s\n", mode);
        }
    }

    // final cleanup before exit
    reap_background_children();
    buffer_deallocate(&cb);

    return 0;
}