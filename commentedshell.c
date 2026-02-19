// unistd gives read, fork, execvp, dup2, close, and other posix calls
#include <unistd.h>

// stdio gives printf, fprintf, perror
#include <stdio.h>

// stdlib gives malloc, free, exit, size_t conversions, and general utilities
#include <stdlib.h>

// string gives strlen, strcmp, strtok
#include <string.h>

// sys/wait gives waitpid and related constants
#include <sys/wait.h>

// errno gives errno values like EINTR and ECHILD
#include <errno.h>

// your helper that implements a circular buffer for robust line reading
#include "circularBuffer.h"

// your helper that splits a command line into argv format for execvp
#include "splitCommand.h"

// how many bytes to read from stdin per read() call
// 1024 is a common compromise, large enough to reduce syscalls, small enough to avoid waste
#define READ_CHUNK 1024

// maximum length of one line we allow to store in our mode or command buffers
// 4096 is a safe choice, big enough for long command lines, small memory cost
#define LINE_MAX   4096

// remove trailing newline or carriage return characters from a string
// we do this because input lines come with '\n', but comparisons like strcmp(mode,"EXIT") need clean strings
static void strip_newline(char *s) {
    // defensive check, prevents crashing if called with null
    if (!s) return;

    // strlen finds the current length of the string
    size_t n = strlen(s);

    // keep removing end characters while they are newline or carriage return
    // carriage return is included to handle windows-style line endings \r\n
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        // replace newline with string terminator so string becomes shorter
        s[n - 1] = '\0';
        n--;
    }
}

// reap finished background children to avoid zombie processes
// concurrent commands do not block, so we need to collect them later using waitpid with WNOHANG
static void reap_background_children(void) {
    // status stores exit code information for reaped children
    int status = 0;

    // loop because multiple children might have finished since the last check
    while (1) {
        // waitpid with pid = -1 means reap any child
        // WNOHANG means do not block, return immediately if no child has finished
        pid_t p = waitpid(-1, &status, WNOHANG);

        // p > 0 means a child was reaped, continue to reap more finished children
        if (p > 0) continue;

        // p == 0 means no finished child right now, stop reaping
        if (p == 0) break;

        // p < 0 is an error case
        // EINTR means interrupted by a signal, retry
        if (errno == EINTR) continue;

        // ECHILD means there are no child processes at all, stop
        if (errno == ECHILD) break;

        // any other error, stop
        break;
    }
}

/*
 * reads the next full line from fd using the circular buffer
 * returns:
 *  1 if a line was read successfully into out
 *  0 if eof and no more data remains
 * -1 on error
 *
 * why do we need this
 * read(fd, buf, N) does not guarantee one full line, especially with redirected input
 * it can return half a line or multiple lines, so we store bytes in a circular buffer until a delimiter appears
 */
static int read_line_cb(CircularBuffer *cb, int fd, char *out, size_t out_cap) {
    // static means this variable persists across calls
    // we need this because eof state depends on previous reads
    static int reachedEOF = 0;

    // temp array for bytes read from stdin in chunks
    unsigned char tmp[READ_CHUNK];

    while (1) {
        // asks the circular buffer if a full element exists
        // delimiter is '\n', so element means one line including newline
        // if reachedEOF is true, it treats remaining bytes as a final line even without newline
        int next_sz = buffer_size_next_element(cb, '\n', reachedEOF);

        // if next_sz != -1, we have a complete line or final leftover data
        if (next_sz != -1) {
            // we must ensure the output buffer can hold the line plus terminator
            // next_sz includes the delimiter if it existed, so it can be as big as the line length
            if ((size_t)next_sz >= out_cap) {
                // if line is too long, we discard it from buffer to keep parsing in sync
                for (int i = 0; i < next_sz; i++) (void)buffer_pop(cb);

                // ENOMEM used here to signal “buffer not big enough”
                errno = ENOMEM;
                return -1;
            }

            // pop exactly next_sz bytes from the circular buffer into out
            for (int i = 0; i < next_sz; i++) {
                out[i] = (char)buffer_pop(cb);
            }

            // add null terminator so out becomes a c string
            out[next_sz] = '\0';

            // remove newline or carriage return at end
            strip_newline(out);

            return 1;
        }

        // if we reach here, there is no complete line yet, so we must read more data
        ssize_t r = read(fd, tmp, sizeof(tmp));

        // r < 0 means read error
        if (r < 0) {
            // EINTR means signal interrupt, retry read
            if (errno == EINTR) continue;
            return -1;
        }

        // r == 0 means end of file
        if (r == 0) {
            // set eof state so buffer_size_next_element can return leftover bytes as final line
            reachedEOF = 1;

            // if buffer is empty, nothing else to read
            if (buffer_used_bytes(cb) == 0) return 0;

            // otherwise loop again, next_sz will become count of leftover bytes
            continue;
        }

        // push all bytes we read into the circular buffer
        for (ssize_t i = 0; i < r; i++) {
            // circular buffer push does not check overflow, so we must check space
            if (buffer_free_bytes(cb) <= 0) {
                // again, ENOMEM means “not enough space in circular buffer”
                errno = ENOMEM;
                return -1;
            }
            buffer_push(cb, tmp[i]);
        }
    }
}

// helper to execute argv in a child process, or exit if it fails
static void exec_child_or_die(char **argv) {
    // if argv is null or empty command, exit cleanly
    // this prevents crashing on blank lines
    if (!argv || !argv[0]) {
        _exit(0); // _exit is preferred in child to avoid flushing stdio buffers twice
    }

    // execvp replaces the process image, if successful it never returns
    // argv[0] is the program, argv is the argument array ending with null
    execvp(argv[0], argv);

    // if execvp returns, it failed, print reason using errno
    perror("execvp");

    // split_command allocated argv array, so free it in the failure path
    // note that tokens inside argv point into the line buffer, so we must not free argv[i]
    free(argv);

    // exit child with failure code
    _exit(1);
}

// run one command line
// wait_for_child = 1 means foreground, wait_for_child = 0 means background
static int run_single(char *cmdline, int wait_for_child) {
    // parse the command line into argv for execvp
    // split_command modifies cmdline in place using strtok
    char **argv = split_command(cmdline);

    // if parsing fails due to malloc failure, report error
    if (!argv) {
        perror("split_command");
        return -1;
    }

    // fork creates a child process, returns 0 in child, pid in parent
    pid_t pid = fork();

    // pid < 0 means fork failed, usually due to system limits
    if (pid < 0) {
        perror("fork");
        free(argv);
        return -1;
    }

    // child branch
    if (pid == 0) {
        // child executes the program, does not return unless exec fails
        exec_child_or_die(argv);
    }

    // parent branch
    if (wait_for_child) {
        int status = 0;

        // waitpid waits specifically for this pid
        // this is required for foreground commands
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            free(argv);
            return -1;
        }
    }

    // parent must free argv pointer array after forking
    // this does not affect the child, because after fork memory is logically copied
    free(argv);

    return 0;
}

// run exactly two commands connected by a pipe, cmd1 | cmd2
static int run_piped(char *cmd1, char *cmd2) {
    // fds[0] will be read end, fds[1] write end
    int fds[2];

    // create kernel pipe object
    if (pipe(fds) < 0) {
        perror("pipe");
        return -1;
    }

    // parse first command
    char **argv1 = split_command(cmd1);
    if (!argv1) {
        perror("split_command");
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    // fork first child for cmd1
    pid_t p1 = fork();
    if (p1 < 0) {
        perror("fork");
        free(argv1);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (p1 == 0) {
        // child 1 must send its stdout into the pipe write end
        // dup2 copies fds[1] onto stdout file descriptor 1
        if (dup2(fds[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }

        // close unused ends in the child
        // important because leaving extra ends open can break pipe eof behavior
        close(fds[0]);
        close(fds[1]);

        // run command 1
        exec_child_or_die(argv1);
    }

    // parse second command
    char **argv2 = split_command(cmd2);
    if (!argv2) {
        perror("split_command");
        free(argv1);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    // fork second child for cmd2
    pid_t p2 = fork();
    if (p2 < 0) {
        perror("fork");
        free(argv1);
        free(argv2);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (p2 == 0) {
        // child 2 must read its stdin from the pipe read end
        // dup2 copies fds[0] onto stdin file descriptor 0
        if (dup2(fds[0], STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }

        // close unused ends in the child
        close(fds[1]);
        close(fds[0]);

        // run command 2
        exec_child_or_die(argv2);
    }

    // parent closes pipe ends because it does not use them
    // this matters so that cmd2 can see eof when cmd1 finishes
    close(fds[0]);
    close(fds[1]);

    // parent frees argv arrays because children either exec or have their own copies
    free(argv1);
    free(argv2);

    // wait for both children because piped mode is foreground by assignment rules
    int status = 0;
    if (waitpid(p1, &status, 0) < 0) perror("waitpid");
    if (waitpid(p2, &status, 0) < 0) perror("waitpid");

    return 0;
}

// main loop of the shell
int main(void) {
    // circular buffer stores raw bytes from stdin across multiple read() calls
    CircularBuffer cb;

    // 8192 gives enough room to handle multiple lines arriving at once
    // it is a practical size, large enough for typical test files, small memory cost
    if (buffer_init(&cb, 8192) != 0) {
        fprintf(stderr, "failed to allocate circular buffer\n");
        return 1;
    }

    // buffers for mode line and one or two command lines
    // LINE_MAX prevents overflow and sets a maximum accepted input line length
    char mode[LINE_MAX];
    char line1[LINE_MAX];
    char line2[LINE_MAX];

    while (1) {
        // clean background children each iteration, prevents zombie accumulation
        reap_background_children();

        // read execution mode line, like SINGLE, PIPED, CONCURRENT, EXIT
        int r = read_line_cb(&cb, STDIN_FILENO, mode, sizeof(mode));

        // r == 0 means eof clean exit
        if (r == 0) break;

        // r < 0 means error reading input
        if (r < 0) {
            perror("read");
            break;
        }

        // remove newline so mode comparisons work
        strip_newline(mode);

        // exit terminates the loop and ends the program
        if (strcmp(mode, "EXIT") == 0) {
            break;
        }

        // single mode, read exactly one command line, run it in foreground
        if (strcmp(mode, "SINGLE") == 0) {
            int r1 = read_line_cb(&cb, STDIN_FILENO, line1, sizeof(line1));
            if (r1 <= 0) break;
            run_single(line1, 1);
        }
        // concurrent mode, read one command line, run it in background
        else if (strcmp(mode, "CONCURRENT") == 0) {
            int r1 = read_line_cb(&cb, STDIN_FILENO, line1, sizeof(line1));
            if (r1 <= 0) break;
            run_single(line1, 0);
        }
        // piped mode, read two command lines, run them as cmd1 | cmd2
        // we accept both PIPED and PIPE because the pdf and example might differ
        else if (strcmp(mode, "PIPED") == 0 || strcmp(mode, "PIPE") == 0) {
            int r1 = read_line_cb(&cb, STDIN_FILENO, line1, sizeof(line1));
            if (r1 <= 0) break;

            int r2 = read_line_cb(&cb, STDIN_FILENO, line2, sizeof(line2));
            if (r2 <= 0) break;

            run_piped(line1, line2);
        }
        // unknown mode, print an error and continue the loop
        else {
            fprintf(stderr, "unknown execution mode %s\n", mode);
        }
    }

    // final cleanup, in case a background child finished right as we are exiting
    reap_background_children();

    // free memory allocated by circular buffer
    buffer_deallocate(&cb);

    return 0;
}
