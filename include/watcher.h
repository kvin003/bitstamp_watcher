#ifndef WATCHER_H
#define WATCHER_H
struct watcher{
    int type;
    int msgnum;
    int tracable;
    pid_t pid;
    int id;
    int in;
    int out;
    int used;
    WATCHER *next;
    char **arg;
};
#endif