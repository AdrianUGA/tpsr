/*
 * Copyright (C) 2002, Simon Nieuviarts
 */

#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

#define DEBUG 0

//TODO Pas de bog avec enter, differencier > et >>


void pass(){}

/*
 * Count the number of sequences for the command <l>
 */
int nb_seq(struct cmdline *l){
    int i;
    for (i = 0; l->seq[i] != 0; i++);
    return i;
}

/*
 * Connects the correct I/Os for the son number <son_number>
 */
int connect_stdios(int son_number, struct cmdline *l, int com_pipe[][2], int com_pipe_size){
    /* Set up stdin */
    if(son_number == 0){
        if(l->in){
            int f = open(l->in, O_RDONLY);
            if(f<0){
                printf("open error. in : %s\n", l->in);
                return -1;
            }
            if(dup2(f, 0) < 0){
                printf("dup2 error. in : %s\n", l->in);
                return -1;
            }
        }
    }else{
        dup2(com_pipe[son_number-1][0], 0);
    }

    /* Set up stdout */
    if(son_number == com_pipe_size-1){
        if(l->out){
            int f = open(l->out, O_WRONLY | O_CREAT ); // TODO set user permissions //TODO File created before command exectution
            if(f<0){
                printf("open error. in : %s\n", l->out);
                return -1;
            }
            if(dup2(f, 1) < 0){
                printf("dup2 error. in : %s\n", l->out);
                return -1;
            }
        }

    }else{
        dup2(com_pipe[son_number][1], 1);
    }
    return 0;
}


/*
 * Close every pipe end unused by the son number <son_number>
 * Returns 0 if succeded, -1 in case of error
 */
int close_unused_com_pipe(int son_number, int com_pipe[][2], int com_pipe_size){
    int i;
    for(i=0; i<com_pipe_size; i++){
        if(i == son_number){
            if(close(com_pipe[i][0]) != 0) return -1;
        }else if (i == son_number-1){
            if(close(com_pipe[i][1]) != 0) return -1;
        }else{
            if(close(com_pipe[i][0]) != 0) return -1;
            if(close(com_pipe[i][1]) != 0) return -1;
        }
    }
    return 0;
}

int do_command(struct cmdline *l, pid_t *not_closed, int *nb_not_closed) {
    
    if(*(l->seq) != 0 && strcmp(l->seq[0][0], "exit") == 0){
        return 1;
    }
    
    int i, status, nb_se = nb_seq(l), com_pipe[nb_se-1][2];
    pid_t child_pid, children_pids[nb_se];
    
    sigset_t signal_set; int sig;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGUSR1);
    signal(SIGUSR1, pass);
    
    for(i=0; i<nb_se-1; i++){
        if(pipe(com_pipe[i]) <0){
            printf("pipe error. errno %d. Number %d\n", errno, i);
        }
    }
    
    
    for (i = 0; l->seq[i] != 0; i++) {
        if(DEBUG) printf("Let's fork\n");
        child_pid = fork();
        if(child_pid == -1){
            printf("Fork error at number %d\n", i);
            return -1;
        }else if(child_pid == 0){ /* Child */
            if(DEBUG) printf("Son %d created\n", i);
            
            if(close_unused_com_pipe(i, com_pipe, nb_se-1) != 0){
                printf("Can't close pipes for son number %d\n", i);
                return -1;
            }
            
            if(connect_stdios(i, l, com_pipe, nb_se) != 0){
                printf("Can't connect stdios for son number %d\n", i);
                return -1;
            }
            
            if(DEBUG) printf("%d - waiting\n", i);
            sigwait(&signal_set, &sig);
            if(DEBUG) printf("%d - executing\n", i);
            /* Execute the command sequence number <i> */
            if(execvp(l->seq[i][0], l->seq[i]) < 0){
                printf("Erreur execvp errno : %d %d\n", errno, ENOENT);
                return-1;
            }
        }else{ /* Father */
            if(DEBUG) printf("Fork %d done\n", i);
            children_pids[i] = child_pid;
        }
    }  
    
    /* Close all pipes for the father */
    for(i=0; i<nb_se-1; i++){
        if(close(com_pipe[i][0]) != 0) return -1;
        if(close(com_pipe[i][1]) != 0) return -1;
    }
    
    /* Start the children */
    sleep(1); //TODO The child never receives the signal otherwise
    for(i=0; i<nb_se; i++){
        kill(children_pids[i], SIGUSR1);
    }
    /* Wait for every children */
    if(DEBUG) printf("Father waiting\n");
    if(!l->background){
        //while (wait(&status) > 0);
        for(i=0; i<nb_se; i++){
            if(DEBUG) printf("Father waiting for %d\n", i);
            if(waitpid(children_pids[i], &status, 0) == -1){
                printf("Error waiting for son %d - pid %d\n", i, children_pids[i]);
            }
        }
    }else{
        not_closed = malloc(nb_se * sizeof(pid_t));
        for(i=0; i<nb_se; i++){
            not_closed[i] = children_pids[i];
        }
        nb_not_closed = nb_se;
    }
    
        
    
    return 0;
}

int main() {
    pid_t *not_closed_global = malloc(0);
    int nb_not_closed_global = 0;
    while (1) {
        struct cmdline *l;
        pid_t *not_closed;
        int nb_not_closed=0;
        int i, j;

        printf("shell> ");
        l = readcmd();

        /* If input stream closed, normal termination */
        if (!l) {
            printf("exit\n");
            exit(0);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        switch(do_command(l, not_closed, &nb_not_closed)){
            case 1:
                free(not_closed);
                free(not_closed_global);
                return 0;
                break;
            case 0:
                if(nb_not_closed != 0){
                    not_closed_global = realloc(not_closed_global, (nb_not_closed+nb_not_closed_global)*sizeof(pid_t));
                    printf("Coucou\n");
                    if(not_closed_global == NULL){
                        printf("Realloc failed\n");
                    }
                    for(i=0; i<nb_not_closed; i++){
                        not_closed_global[nb_not_closed_global + i] = not_closed[i];
                    }
                    nb_not_closed_global += nb_not_closed;
                    free(not_closed);
                }
                break;
            default:
                printf("Command failed\n");
                break;
             
        }
        continue;
        
        
        if (l->in) printf("in: %s\n", l->in);
        if (l->out) printf("out: %s\n", l->out);

        
        
        /* Display each command of the pipe */
        for (i = 0; l->seq[i] != 0; i++) {
            char **cmd = l->seq[i];
            printf("seq[%d]: ", i);
            for (j = 0; cmd[j] != 0; j++) {
                printf("%s ", cmd[j]);
            }
            printf("\n");
        }
    }
}
