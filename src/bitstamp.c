#include <stdlib.h>
#include "ticker.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <debug.h>
#include "store.h"
#include "argo.h"
#include "watcher.h"
#include <errno.h>

extern WATCHER *first;

static int valid_msg(char *msg);
static char *json_string(char *msg);
WATCHER *bitstamp_watcher_start(WATCHER_TYPE *type, char *args[]) {
    WATCHER *bitstamp;
    int types = 0;
    WATCHER_TYPE *watch = watcher_types;
    while(strcmp(watch->name, type->name)){
        types++;
        watch++;
    }
    debug("%d", types);
    int olderrno = errno;
    int fd[2];
    int fd2[2];
    if(pipe(fd) < 0)
        return NULL;
    if(pipe(fd2) < 0)
        return NULL;
    pid_t pid = fork();
    if(pid < 0){
        return NULL;
    }if(pid == 0){
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);
        dup2(fd2[0], STDIN_FILENO);
        close(fd2[0]);
        close(fd2[1]);
        if(execvp(type->argv[0], type->argv) < 0)
            exit(1);
    }else{
        int argnum = 0;
        char **arg = args;
        while(*arg){
            argnum++;
            arg++;
        }
        arg = args;
        char **totArg = malloc(sizeof(char *) * (argnum + 1));
        for(int i = 0; i < argnum; i++){
            char *argument = malloc((strlen(args[i]) + 1) * sizeof(char));
            strcpy(argument, args[i]);
            totArg[i] = argument;
        }
        totArg[argnum] = NULL;
        WATCHER *temp= first;
        int found = 0;
        while(temp != NULL){
            if(!(temp->used)){
                found = 1;
                break;
            }if(temp->next != NULL){
                temp = temp->next;
            }else
                break;
        }
        if(found){
            bitstamp = temp;
            bitstamp->type = types;
            bitstamp->pid = pid;
            bitstamp->in = fd[0];
            bitstamp->out = fd2[1];
            bitstamp->msgnum = 0;
            bitstamp->used = 1;
            bitstamp->tracable = 0;
            bitstamp->arg = totArg;
        }else{
            bitstamp = malloc(sizeof(WATCHER));
            if(bitstamp == NULL)
                return NULL;
            bitstamp->type = types;
            bitstamp->pid = pid;
            bitstamp->in = fd[0];
            bitstamp->out = fd2[1];
            bitstamp->msgnum = 0;
            bitstamp->used = 1;
            bitstamp->tracable = 0;
            bitstamp->arg = totArg;
            temp->next = bitstamp;
            bitstamp->id = temp->id + 1;
            bitstamp->next = NULL;
        }
        close(fd[1]);
        close(fd2[0]);
        char buffer[1024];
        fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_ASYNC | O_NONBLOCK);
        fcntl(fd[0], F_SETOWN, getpid());
        fcntl(fd[0], F_SETSIG, SIGIO);
        if(errno != olderrno){
            free(bitstamp);
            return NULL;
        }sprintf(buffer, "{ \"event\": \"bts:subscribe\", \"data\": { \"channel\": \"%s\" } }\n", args[0]);
        watcher_types[bitstamp->type].send(bitstamp, buffer);
    }
    return bitstamp;
}

int bitstamp_watcher_stop(WATCHER *wp) {
    wp->used = 0;
    close(wp->in);
    close(wp->out);
    char **arguments = wp->arg;
    char *arg;
    while((arg = *arguments) != NULL){
        free(arg);
        arguments++;
    }
    free(wp->arg);
    kill(wp->pid, SIGTERM);
    return 0;
}

int bitstamp_watcher_send(WATCHER *wp, void *arg) {
    char *msg = (char *)arg;
    //debug("%s", msg);
    write(wp->out, msg, strlen(msg));
    //close(wp->in);
    return 0;
}

int bitstamp_watcher_recv(WATCHER *wp, char *txt) {
    //debug("from bitstamp");
    if(!strcmp(txt, "> ")){
        return 4;
    }
    wp->msgnum++;
    if(wp->tracable){
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        if(strncmp("\b\b", txt, 2))
            fprintf(stderr, "[%-10ld.%06ld][%s][%2d][%5d]: %s\n\n", ts.tv_sec, ts.tv_nsec/1000, watcher_types[wp->type].name, wp->in, wp->msgnum, txt);
        else
            fprintf(stderr, "[%-10ld.%06ld][%s][%2d][%5d]: > %s\n\n", ts.tv_sec, ts.tv_nsec/1000, watcher_types[wp->type].name, wp->in, wp->msgnum, txt);
    }
    debug("%s", txt);
    if(!valid_msg(txt)){
        char *parsed = json_string(txt);
        FILE *stream;
        char *buf;
        size_t len;
        stream = open_memstream(&buf, &len);
        fprintf(stream, "%s", parsed);
        fflush(stream);
        debug("%s", buf);
        free(parsed);
        ARGO_VALUE *json = argo_read_value(stream);
        if(json != NULL){
            ARGO_VALUE *trade = argo_value_get_member(json, "event");
            if(trade != NULL){
                char *event = argo_value_get_chars(trade);
                debug("%s", event);
                if(event != NULL){
                    if(!strcmp(event, "trade")){
                        ARGO_VALUE *data = argo_value_get_member(json, "data");
                        if(data != NULL){
                            ARGO_VALUE *volume = argo_value_get_member(data, "amount");
                            double volumes;
                            argo_value_get_double(volume, &volumes);
                            debug("%lf", volumes);
                            ARGO_VALUE *price = argo_value_get_member(data, "price");
                            double prices;
                            argo_value_get_double(price, &prices);
                            debug("%lf", prices);
                            FILE *volumeStream;
                            FILE *priceStream;
                            char *volumeBuf;
                            char *priceBuf;
                            size_t volumeLen;
                            size_t priceLen;
                            volumeStream = open_memstream(&volumeBuf, &volumeLen);
                            priceStream = open_memstream(&priceBuf, &priceLen);
                            fprintf(volumeStream, "%s:%s:volume",watcher_types[wp->type].name, wp->arg[0]);
                            fflush(volumeStream);
                            fprintf(priceStream, "%s:%s:price", watcher_types[wp->type].name, wp->arg[0]);
                            fflush(priceStream);
                            debug("%s", priceBuf);
                            debug("%s", volumeBuf);
                            struct store_value priceValue;
                            priceValue.type = STORE_DOUBLE_TYPE;
                            priceValue.content.double_value = prices;
                            store_put(priceBuf, &priceValue);
                            struct store_value *volumeValue = store_get(volumeBuf);
                            if(volumeValue == NULL){
                                struct store_value newVolume;
                                newVolume.type = STORE_DOUBLE_TYPE;
                                newVolume.content.double_value = volumes;
                                store_put(volumeBuf, &newVolume);
                            }else{
                                double newVolume = volumeValue->content.double_value;
                                newVolume += volumes;
                                volumeValue->content.double_value = newVolume;
                                store_put(volumeBuf, volumeValue);
                                store_free_value(volumeValue);
                            }
                            fclose(volumeStream);
                            fclose(priceStream);
                            free(volumeBuf);
                            free(priceBuf);
                            //argo_free_value(volume);
                            //argo_free_value(price);
                            //argo_free_value(data);
                        }
                    }   
                    free(event);
                }
                //argo_free_value(trade);
            }
        }
        argo_free_value(json);
        fclose(stream);
        free(buf);
    }return 4;
}

int bitstamp_watcher_trace(WATCHER *wp, int enable) {
    if(enable)
        wp->tracable = 1;
    else
        wp->tracable = 0;
    return 0;
}

static int valid_msg(char *msg){
    //debug("%s",msg);
    char *prefix = "\b\bServer message: '";
    int res = strncmp(prefix, msg, strlen(prefix));
    //debug("%d", res);
    if(res)
        return 1;
    else{
        char *temp = msg;
        temp += (strlen(msg) - 1);
        if(*temp == '\'')
            return 0;
        else
            return 1;
    }
    return 0;
}
static char *json_string(char *msg){
    char *prefix = "\b\bServer message: '";
    char *fin = msg + strlen(prefix);
    char *res = malloc(strlen(fin)* sizeof(char));
    memcpy(res, fin, strlen(fin));
    res[strlen(fin) - 1] = '\0';
    return res;
}