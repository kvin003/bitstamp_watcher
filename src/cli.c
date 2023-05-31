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
#include "watcher.h"


WATCHER *cli_watcher_start(WATCHER_TYPE *type, char *args[]) {
    WATCHER *cli = malloc(sizeof(WATCHER));
    if(cli == NULL)
        return NULL;
    cli->id = 0;
    cli->in = 0;
    cli->out = 1;
    cli->type = CLI_WATCHER_TYPE;
    cli->pid = -1;
    cli->next = NULL;
    cli->used = 1;
    cli->arg = NULL;
    cli->msgnum = 0;
    cli->tracable = 0;
    return cli;
}

int cli_watcher_stop(WATCHER *wp) {
    free(wp);
    return 0;
}

int cli_watcher_send(WATCHER *wp, void *arg) {
    char *sent = (char *)arg;
    if(write(wp->out, sent, strlen(sent)) < 0)
        return 1;
    return 0;

}

int cli_watcher_recv(WATCHER *wp, char *txt) {
    wp->msgnum++;
    if(wp->tracable){
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(stderr, "[%-10ld.%06ld][%s][%2d][%5d]: %s\n\n", ts.tv_sec, ts.tv_nsec/1000, watcher_types[wp->type].name, wp->in, wp->msgnum, txt);
    }
    if(!strcmp("quit", txt)){
        WATCHER *quitting = wp;
            while(quitting != NULL){
                if(quitting->used && (quitting->type != CLI_WATCHER_TYPE))
                    watcher_types[quitting->type].stop(quitting);
                WATCHER *next = quitting->next;
                if(quitting->type != CLI_WATCHER_TYPE)
                    free(quitting);
                quitting = next;
            }
        return 1;
    }if(!strcmp("watchers", txt)){
        return 2;
    }else{
        char buffer[strlen(txt) + 1];
        strcpy(buffer, txt);
        int argNum = 0;
        char *tempArg = strtok(buffer, " \t\n\v\f\r");
        while(tempArg != NULL){
            tempArg = strtok(NULL, " \t\n\v\f\r");
            argNum++;
        }if(argNum == 0)
            return 3;
        strcpy(buffer, txt);
        FILE *args[argNum];
        char *buf[argNum];
        size_t len[argNum];
        for(int i = 0; i < argNum; i++)
            args[i] = open_memstream(&buf[i], &len[i]);
        tempArg = strtok(buffer, " \t\n\v\f\r");
        fprintf(args[0], "%s", tempArg);
        fflush(args[0]);
        for(int i = 1; i < argNum; i++){
            tempArg = strtok(NULL, " \t\n\v\f\r");
            fprintf(args[i], "%s", tempArg);
            fflush(args[i]);
        }
        if(!strcmp("start", buf[0])){
            if(argNum < 2){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }if(!strcmp("CLI", buf[1])){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            WATCHER_TYPE type;
            int found = 0;
            int index = 0;
            while((type = watcher_types[index]).name != 0){
                if(!strcmp(type.name, buf[1])){
                    found = 1;
                    break;
                }index++;
            }if(!found){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }
                return 3;
            }if(!strcmp(type.name, "bitstamp.net") && argNum < 3){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }
                return 3;
            }
            char *arguments[argNum - 1];
            for(int i = 0; i < argNum - 2; i++){
                arguments[i] = buf[i + 2];
            }
            arguments[argNum - 2] = NULL;
            WATCHER *watch = type.start(&type, arguments);
            if(watch == NULL){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }
                return 3;
            }
            for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 4;
        }else if(!strcmp("trace", buf[0])){
            if(argNum != 2){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            int id = atoi(buf[1]);
            if(id == 0 && ((!isdigit(*buf[1])) || (*(buf[1] + 1) != '\0'))){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            WATCHER *temp = wp;
            int found = 0;
            while(temp != NULL){
                if(temp->id == id && temp->used){
                    found = 1;
                    break;
                }
                temp = temp->next;
            }
            if(!found){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }watcher_types[wp->type].trace(temp, 1);
            for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 4;
        }else if(!strcmp("untrace", buf[0])){
            if(argNum != 2){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            int id = atoi(buf[1]);
            if(id == 0 && ((!isdigit(*buf[1])) || (*(buf[1] + 1) != '\0'))){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            WATCHER *temp = wp;
            int found = 0;
            while(temp != NULL){
                if(temp->id == id && temp->used){
                    found = 1;
                    break;
                }
                temp = temp->next;
            }
            if(!found){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }watcher_types[wp->type].trace(temp, 0);
            for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 4;
        }else if(!strcmp("show", buf[0])){
            if(argNum != 2){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            struct store_value *value = store_get(buf[1]);
            if(value == NULL){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            double amount = value->content.double_value;
            fprintf(stdout, "%s\t%lf\n", buf[1], amount);
            for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }store_free_value(value);
            return 4;
        }else if(!strcmp("stop", buf[0])){
            if(argNum != 2){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            int id = atoi(buf[1]);
            if(id == 0){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            WATCHER *temp = wp;
            int found = 0;
            while(temp != NULL){
                if(temp->id == id && temp->used){
                    found = 1;
                    break;
                }
                temp = temp->next;
            }
            if(!found){
                for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
            }
            watcher_types[temp->type].stop(temp);
            for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 4;
        }else{
            for(int i = 0; i < argNum; i++){
                    fclose(args[i]);
                    free(buf[i]);
                }return 3;
        }
    }
    return 3;
}

int cli_watcher_trace(WATCHER *wp, int enable) {
    if(enable)
        wp->tracable = 1;
    else
        wp->tracable = 0;
    return 0;
}
