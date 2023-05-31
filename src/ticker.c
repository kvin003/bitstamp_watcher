#include <stdlib.h>
#include "ticker.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>
#include "watcher.h"

WATCHER *first;
void sigchld_handler(int sig, siginfo_t *info, void *ucontext){
    debug("reap");
    waitpid(-1, NULL, WNOHANG);
    //debug("%d", id);
    //debug("killed %d", id);
    /*WATCHER *temp = first;
    while(temp != NULL){
        debug("searching %d", id);
        if(temp->pid == id){
            close(temp->in);
            close(temp->out);
            temp->used = 0;
        }
        temp = temp->next;
    }*/
}
int fd_num;
volatile sig_atomic_t reading;
int hasInput;
//int moreInput;
int end_reached;
static int firstRun = 1;
void sigio_handler(int sig, siginfo_t *info, void *ucontext_t){
   reading = 1;
}
void sigint_handler(int sig){
    debug("recieved SIGINT");
    WATCHER *temp = first;
    while(temp != NULL){
        if(temp->used && (temp->type != CLI_WATCHER_TYPE)){
            pid_t pid = temp->pid;
            kill(pid, SIGTERM);
            waitpid(pid, NULL, WNOHANG);
        }if((temp->type != CLI_WATCHER_TYPE) && temp->used){
            close(temp->in);
            close(temp->out);
            char **arguments = temp->arg;
            char *arg;
            while((arg = *arguments) != NULL){
                free(arg);
                arguments++;
            }
            free(temp->arg);
        }WATCHER *next = temp->next;
        if(temp->type != CLI_WATCHER_TYPE)
            free(temp);
        temp = next;
    }
    free(first);
    exit(0);
}
static void print_watcher(WATCHER *wp);
char *prompt = "ticker> ";
char *err = "???\n";
int ticker(void) {
    sigset_t mask, prev;
    int callres;
    callres = sigemptyset(&mask);
    if(callres < 0)
        exit(1);
    callres = sigaddset(&mask, SIGIO);
    if(callres < 0)
        exit(1);
    //sigaddset(&mask, SIGCHLD);
    callres = sigprocmask(SIG_BLOCK, &mask, &prev);
    if(callres < 0)
        exit(1);
    callres = fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_ASYNC | O_NONBLOCK);
    if(callres < 0)
        exit(1);
    callres = fcntl(STDIN_FILENO, F_SETOWN, getpid());
    if(callres < 0)
        exit(1);
    callres = fcntl(STDIN_FILENO, F_SETSIG, SIGIO);
    if(callres < 0)
        exit(1);
    //sigemptyset(&prev);
    struct sigaction saio = {0};
    struct sigaction sachld = {0};
    struct sigaction saint = {0};
    callres = sigemptyset(&saio.sa_mask);
    if(callres < 0)
        exit(1);
    callres = sigaddset(&saio.sa_mask, SIGIO);
    if(callres < 0)
        exit(1);
    saio.sa_flags = SA_SIGINFO;
    saio.sa_sigaction = sigio_handler;
    callres = sigaction(SIGIO, &saio, NULL);
    if(callres < 0)
        exit(1);
    callres = sigemptyset(&sachld.sa_mask);
    if(callres < 0)
        exit(1);
    callres = sigaddset(&sachld.sa_mask, SIGCHLD);
    if(callres < 0)
        exit(1);
    sachld.sa_flags = SA_SIGINFO;
    sachld.sa_sigaction = sigchld_handler;
    callres = sigaction(SIGCHLD, &sachld, NULL);
    if(callres < 0)
        exit(1);
    callres = sigemptyset(&saint.sa_mask);
    if(callres < 0)
        exit(1);
    callres = sigaddset(&saint.sa_mask, SIGINT);
    if(callres < 0)
        exit(1);
    saint.sa_handler = sigint_handler;
    callres = sigaction(SIGINT, &saint, NULL);
    if(callres < 0)
        exit(1);
    first = watcher_types[CLI_WATCHER_TYPE].start(&watcher_types[CLI_WATCHER_TYPE], NULL);
    FILE *stream;
    char *buf;
    size_t len;
    //off_t eob;
    stream = open_memstream(&buf, &len);
    if(stream == NULL)
        exit(1);
    raise(SIGIO);
    int stdin_input = 0;
    FILE *partial;
    char *partialBuf;
    size_t partialLen;
    int partialRead = 0;
    partial = open_memstream(&partialBuf, &partialLen);
    while(1){
        //sigprocmask(SIG_SETMASK, &prev, NULL);
        hasInput = stdin_input;
        if(firstRun)
            watcher_types[CLI_WATCHER_TYPE].send(first, prompt);
        else if(hasInput && fd_num == STDIN_FILENO)
            watcher_types[CLI_WATCHER_TYPE].send(first, prompt);
        sigsuspend(&prev);
        WATCHER *readFrom = first;
        int old_errno = errno;
        while(readFrom != NULL){
            if(readFrom->used)
                fd_num = readFrom->in;
            else{
                readFrom = readFrom->next;
                continue;
            }
            WATCHER *cur = readFrom;
            readFrom = readFrom->next;
            firstRun = 0;
            hasInput = 0;
            while(errno != EWOULDBLOCK){
                char c[4097] = {0};
                int res;
                if((res = read(fd_num, c, 4096)) == 0){
                    end_reached = 1;
                    break;
                }else if(res == -1){
                    break;
                }
                hasInput++;
                //debug("reading %d, %c", c, c);
                fprintf(stream, "%s", c);
                fflush(stream);
            }if(errno == EWOULDBLOCK){
                int t;
                if(read(fd_num, &t, sizeof(char)) == 0)
                    end_reached = 1;
            }errno = old_errno;
            fprintf(stream, "%c", 0);
            fflush(stream);
            fseeko(stream, 0, SEEK_SET);
            if(strchr(buf, '\n') == NULL && hasInput && fd_num == STDIN_FILENO){
                partialRead = 1;
                fprintf(partial, "%s", buf);
                fflush(partial);
                debug("%s", partialBuf);
                stdin_input = 0;
                continue;
            }else if(strchr(buf, '\n') && fd_num == STDIN_FILENO && hasInput && partialRead){
                partialRead = 0;
                fprintf(partial, "%s", buf);
                fprintf(partial, "%c", 0);
                fflush(partial);
                debug("%s", partialBuf);
                fseeko(partial, 0, SEEK_SET);
                fprintf(stream, "%s%c", partialBuf, 0);
                fflush(stream);
                fseeko(stream, 0, SEEK_SET);
            }
            char bufmp[strlen(buf) + 1];
            strcpy(bufmp, buf);
            char *token;
            debug("%s", bufmp);
            token = strtok(bufmp, "\n");
            if(hasInput && token == NULL)
                watcher_types[CLI_WATCHER_TYPE].send(first, err);
            WATCHER *temp = first;
            int type;
            while(temp != NULL){
                if(temp->in == fd_num){
                    type = temp->type;
                    break;
                }temp = temp->next;
            }
            int argnum = 0;
            while(token != NULL){
                argnum++;
                token = strtok(NULL, "\n");
            }
            if(argnum){
                char *args[argnum];
                strcpy(bufmp, buf);
                token = strtok(bufmp, "\n");
                for(int i = 0; i < argnum; i++){
                    args[i] = token;
                    token = strtok(NULL, "\n");
                }
                for(int i = 0 ; i < argnum; i++){
                    int res = watcher_types[type].recv(temp, args[i]);
                    if(res == 1){
                        watcher_types[CLI_WATCHER_TYPE].send(first, prompt);
                        free(first);
                        fclose(stream);
                        free(buf);
                        fclose(partial);
                        free(partialBuf);
                        exit(0);
                    }else if(res == 2){
                        print_watcher(first);
                    }else if(res == 3)
                        watcher_types[CLI_WATCHER_TYPE].send(first, err);
                    if((i != argnum - 1) && fd_num == STDIN_FILENO)
                        watcher_types[CLI_WATCHER_TYPE].send(first, prompt);
                }
            }
            if(end_reached && fd_num == STDIN_FILENO){
                if(hasInput)
                    watcher_types[CLI_WATCHER_TYPE].send(first, prompt);
                WATCHER *start = first;
                while(start != NULL){
                    if(start->used && (start->type != CLI_WATCHER_TYPE))
                        kill(start->pid, SIGTERM);
                    if((start->type != CLI_WATCHER_TYPE) && start->used){
                        char **arguments = start->arg;
                        char *arg;
                        while((arg = *arguments) != NULL){
                            free(arg);
                            arguments++;
                        }
                        free(start->arg);
                    }
                    WATCHER *next = start->next;
                    free(start);
                    start = next;
                }
                fclose(stream);
                free(buf);
                fclose(partial);
                free(partialBuf);
                exit(0);
            }else if(end_reached && fd_num != STDIN_FILENO){
                cur->used = 0;
                close(cur->in);
                close(cur->out);
                char **argument = cur->arg;
                char *arg;
                while((arg = *argument) != NULL){
                    free(arg);
                    argument++;
                }
                free(cur->arg);
                end_reached = 0;
            }
            if(fd_num == STDIN_FILENO)
                stdin_input = hasInput;
        }
        fd_num = STDIN_FILENO;
    }
        fclose(stream);
        free(buf);
        fclose(partial);
        free(partialBuf);
        return 0;
    }


static void print_watcher(WATCHER *wp){
    while(wp != NULL){
        WATCHER_TYPE types = watcher_types[wp->type];
        if(wp->type == CLI_WATCHER_TYPE){
            fprintf(stdout, "%d\t%s(%d,%d,%d)\n", wp->id, types.name, wp->pid, wp->in, wp->out);
            fflush(stdout);
        }else if(wp->used){
            fprintf(stdout, "%d\t%s(%d,%d,%d) ", wp->id, types.name, wp->pid, wp->in, wp->out);
            char **temp;
            for(temp = types.argv; *temp; temp++)
                fprintf(stdout, "%s ", *temp);
            fprintf(stdout, "[");
            for(temp = wp->arg; *temp; temp++){
                fprintf(stdout, "%s", *temp);
                if(*(temp + 1) != NULL)
                    fprintf(stdout, " ");
            }fprintf(stdout, "]\n");
            fflush(stdout);
        }
        wp = wp->next;
    }
}
