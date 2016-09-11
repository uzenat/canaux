#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include "channel.h"


struct timespec start, end;
#define MILLI_SECONDES 1000

int flag_pipe=0;
int flag_channel=0;


struct channel *channel;
int tube [2];

int counter= 0;

int BUFSIZE= 100;
int NDATA= 1000;
int nWriter=10;
int nReader=45;

sig_atomic_t wsync=0; //nb d'écrivains qui a fini d'écrire sa donnée
sem_t lock;


void *reader (void *p);
void *writer (void *p);

int main (int argc, char **argv) {

  if (argc != 8)
    { fprintf
	(stderr,
	 "usage: %s --reader n --writer m --data d [ --async | --async1 | --sync | --pipe ]\n",
	 argv[0]); exit (1); }

  int i;

  /* traitement des argument */
  for (i=1; i<argc;i++) {

    if (strcmp ("--reader", argv[i])==0) {

      nReader= atoi (argv[i+1]);
      i++;
      
    } else if (strcmp ("--writer", argv[i])==0) {

      nWriter= atoi (argv[i+1]);
      i++;
      
    } else if (strcmp ("--data", argv[i])==0) {

      NDATA= atoi (argv[i+1]);
      i++;

    }else if (strcmp ("--bufsize", argv[i])==0) {

      BUFSIZE= atoi (argv[i+1]);
      i++;
      
    } else if (strcmp ("--pipe", argv[i])==0) {

      flag_pipe=1;
      pipe (tube);
      
    } else if (strcmp ("--sync", argv[i])==0) {

      flag_channel=1;
      channel=
	channel_create (sizeof (int), 0, 0);
      if (channel == NULL)
	{ perror ("channel"); exit (1); }

    } else if (strcmp ("--async", argv[i])==0) {

      flag_channel=1;
      channel=
	channel_create (sizeof (int), BUFSIZE, 0);
      if (channel == NULL)
	{ perror ("channel"); exit (1); }

    } else if (strcmp ("--async1", argv[i])==0) {

      flag_channel=1;
      channel=
	channel_create (sizeof (int), BUFSIZE, CHANNEL_PROCESS_ONECPY);
      if (channel == NULL)
	{ perror ("channel"); exit (1); }
      
      
    } else {
      
      fprintf
	(stderr,
	 "None",
	 argv[0]); exit (1);
      
    }
    
  }  
  
  pthread_t *tWriter=
    calloc (nWriter, sizeof (pthread_t));
   pthread_t *tReader=
    calloc (nReader, sizeof (pthread_t));

  

  sem_init (&lock, 0, 1);

  clock_gettime(CLOCK_MONOTONIC, &start);
  
  for (i=0;i<nWriter; i++)
    pthread_create (&tWriter[i], NULL, writer, NULL);   
  for (i=0;i<nReader; i++)
    pthread_create (&tReader[i], NULL, reader, NULL);   
 
  for (i=0;i<nWriter; i++)
    pthread_join (tWriter[i], NULL);
  for (i=0;i<nReader; i++)
    pthread_join (tReader[i], NULL);

  clock_gettime(CLOCK_MONOTONIC, &end);


  printf("[Time]: %.2lfs\n",
	 ((double)end.tv_sec - start.tv_sec) +
	 ((double)end.tv_nsec - start.tv_nsec) / 1.0E9);
  
  printf ("[COUNTER]: %d\n", counter);


}

void *writer (void *p) {

  int i;
  int *data= malloc (sizeof (int));
  *data=42;

  if (flag_channel==1) { //si on utilise les channels
    for (i=0;i<NDATA;i++)
      channel_send (channel, data);
    
  } else { //sinon pipe
    
    for (i=0;i<NDATA;i++)
      write (tube [1], data, sizeof(int));
    
  }

  sem_wait (&lock);
  wsync ++;
  if (wsync == nWriter)//si l'écrivain a fini son tafœ
    {
      if (flag_channel == 1) //si c'est un channel
	channel_close (channel); //il ferme le channel
      else close (tube[1]); //sinon (pipe) il ferme le pipe
    }
  sem_post (&lock);

}

void *reader (void *p) {

  int x;

  if (flag_channel == 1) {
  
    while (channel_recv (channel, &x)==1) 
      { sem_wait (&lock); counter ++; sem_post (&lock); }

  } else {

    while (read (tube[0], &x, sizeof(int))>0) 
      { sem_wait (&lock); counter ++; sem_post (&lock); }

  }
    
  return NULL;

}
