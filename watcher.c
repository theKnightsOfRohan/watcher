#include <assert.h>
#include <errno.h>
#include <inotifytools/inotifytools.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

// watcher --cmd="gcc main.c -o a.out" main.c

struct argmap {
    char *cmd;
    char **file_list;
    size_t len;
};

struct watch {
    char *cmd;
    char **files;
    int *fds;
    size_t len;
    int fd;
};

struct argmap parse_args(int argc, char **argv);
char *split_non_escaped(char *str, char delim);
bool starts_with(char *str, const char *prefix);
void handle_unknown_file(char *fname);
void usage();
void update_watch(struct watch *watch, int to_update);
void run_command(char *command);

int main(int argc, char **argv) {
    printf("argc: %d\n", argc);

    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }

    // Remove first arg, which is always command name
    struct argmap args = parse_args(argc - 1, argv + 1);

    printf("cmd = %s\n", args.cmd);

    struct watch watch;
    watch.fd = inotify_init();
    watch.fds = malloc(sizeof(int) * args.len);
    watch.files = args.file_list;
    watch.cmd = args.cmd;

    for (int i = 0; i < args.len; i++) {
        watch.fds[i] =
            inotify_add_watch(watch.fd, watch.files[i],
                              IN_MODIFY | IN_DELETE_SELF | IN_CLOSE_WRITE);
        printf("%s = %d\n", watch.files[i], watch.fds[i]);
    }

    char buf[4096];
    struct inotify_event *event;
    size_t readlen;
    for (;;) {
        readlen = read(watch.fd, buf, sizeof(buf));
        if (readlen == -1) {
            printf("inotify read failed with errno %d\n", errno);
            exit(errno);
        }
        printf("read %zu bytes\n", readlen);

        for (char *ptr = buf; ptr < buf + readlen;
             ptr += sizeof(struct inotify_event) + event->len) {
            event = (struct inotify_event *)ptr;

            printf("Name len = %d\n", event->len);

            switch (event->mask) {
            case IN_MODIFY:
                printf("Watched file %d modified\n", event->wd);
                run_command(watch.cmd);
                break;
            case IN_CLOSE_WRITE:
                printf("Watched file %d closed\n", event->wd);
                run_command(watch.cmd);
                break;
            case IN_DELETE_SELF:
                printf("Watched file %d deleted\n", event->wd);
                run_command(watch.cmd);
                break;
            case IN_IGNORED:
                printf("Watched file %d ignored\n", event->wd);
                update_watch(&watch, event->wd);
                break;
            default:
                printf("Unrecognized event 0x%x on file %d\n", event->mask,
                       event->wd);
            }
        }
    }

    return 0;
}

void run_command(char *cmd) {
    int err = system(cmd);
    printf("cmd \"%s\" ran with err %d and errno %d\n", cmd, err, errno);
}

void update_watch(struct watch *watch, int to_update) {
    int i;
    for (i = 0; i < watch->len; i++) {
        if (watch->fds[i] == to_update)
            break;
    }

    assert(i != watch->len);

    int old_fd = watch->fds[i];

    watch->fds[i] =
        inotify_add_watch(watch->fd, watch->files[i],
                          IN_MODIFY | IN_DELETE_SELF | IN_CLOSE_WRITE);

    printf("reset %d to %d\n", old_fd, watch->fds[i]);
}

struct argmap parse_args(int argc, char **argv) {
    struct argmap res = {0, 0, 0};
    // One arg should be the command
    res.len = argc - 1;
    res.file_list = calloc(res.len, sizeof(char *));
    int file_idx = 0;

    char cmd_prefix[] = "--cmd=";
    size_t cmd_len = sizeof(cmd_prefix);

    char *str;
    bool found_cmd = 0;
    for (int i = 0; i < argc; i++) {
        str = argv[i];

        if (strncmp(str, cmd_prefix, cmd_len - 1) == 0) {
            assert(found_cmd == false);
            res.cmd = str + cmd_len - 1;
            found_cmd = true;
            continue;
        }

        if (access(str, F_OK) != 0) {
            free(res.file_list);
            handle_unknown_file(str);
        }

        res.file_list[file_idx] = str;
        file_idx++;
    }

    return res;
}

void usage() { printf("TBD"); }

void handle_unknown_file(char *filename) {
    printf("Unrecognized file: %s\n", filename);
    usage();
    exit(EXIT_FAILURE);
}
