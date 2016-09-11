

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include "channel.h"


#define CHANNEL_PROCESS_SHARED 1
#define CHANNEL_PROCESS_ONECPY 2


#define ASYNC_ONECPY_SEND(chan, data) {			\
    chan -> buffer [chan->pos_w]= data;			\
  }							\
  

#define ASYNC_ONECPY_RECV(chan, data) {				\
    memcpy(data, chan->buffer[chan->pos_r], chan->eltsize);	\
  }								\
  

  
#define ASYNC_SEND(chan, channel, data) {			\
    memcpy(channel->mem + chan->pos_w, data, chan->eltsize);	\
  }								\
  

#define ASYNC_RECV(chan, channel, date) {				\
    memcpy(data, channel->mem + chan->pos_r, chan->eltsize);		\
  }									\



struct header {
  
  int pos_r; /* position de la prochaine valeur à lire */
  int pos_w; /* position de la prochaine valeur à écrire */
  int pos_deb; /* position du debut du buffer */

  int size; /* nombre maximum de données que peut contenir le canal */
  int eltsize; /* taille d'une donnée */
  int incr; /* valeur d'incrémentation pour naviguer dans le canal */
  
  int waiter_r; /* nombre de lecteurs endormis */
  int waiter_w; /* nombre d'écrivains endormis */

  /* sémaphores */
  sem_t lock;
  sem_t sem_a;
  sem_t sem_c;
  sem_t sem_d;

  int close; /* valeur booléenne pour la fermeture du canal */
  int flags; /* CHANNEL_PROCESS_SHARED, CHANNEL_PROCESS_ONECPY, 0 */

  void *addr_sync; /* pointeur pour les canaux synchrones */
  
  const void **buffer; /* buffer pour les ones copy */
    
};


struct channel { void *mem; };


//--------- Pour les canaux non apparenté ------------------------------
struct channel *
channel_unrelated_open (int len, char *path){

  void *mem;
  struct channel *channel;
  int fd;
  
  channel= malloc ( sizeof(struct channel) );

  if ( (fd=shm_open (path, O_RDWR, 0666))<0 )
    { perror ("shm_open"); return NULL; }
    
  mem= 
    mmap(NULL,
	 len,
	 PROT_WRITE|PROT_READ,
	 MAP_SHARED,fd,0);
  
  if(mem==MAP_FAILED) return NULL;
  
  channel->mem= mem;
  return channel;

}


struct channel *
channel_unrelated_create (int eltsize, int size, char *path){
  
  struct header *chan;
  struct channel *channel;
  void *mem;

  int fd;

  channel= malloc ( sizeof (struct channel) );

  if ( (fd=shm_open (path, O_CREAT | O_TRUNC | O_RDWR, 0666))<0 )
    { perror ("shm_open"); return NULL; }
  
  if (ftruncate (fd, sizeof (struct header) + (eltsize * size)))
    { perror ("ftruncate"); return NULL; }

  mem= 
    mmap(NULL,
	 (size == 0)?
	 sizeof(struct header):
	 sizeof (struct header) + size*eltsize,
	 PROT_WRITE|PROT_READ,
	 MAP_SHARED,fd,0);

  if(mem==MAP_FAILED) return NULL;

  chan = (struct header *) mem;

  sem_init (&chan->lock, 1, 1);
  
  sem_init (&chan->sem_a, 1, 0);
  sem_init (&chan->sem_c, 1, 0);
  sem_init (&chan->sem_d, 1, 0);

  chan -> pos_deb = sizeof(struct header);
  chan -> pos_w = sizeof(struct header);
  chan -> pos_r = -1;
  chan -> size = size * eltsize;
  chan -> incr = eltsize;
  chan -> eltsize = eltsize;
  chan -> waiter_w= 0;
  chan -> waiter_r= 0;
  chan -> close = 0;

  chan -> addr_sync = NULL;
  chan -> buffer = NULL;

  channel->mem= mem;
  
  return channel;
  
}

//----------------------------------------------------------------------


struct channel *
channel_create (int eltsize, int size, int flags){

  if(size <0 || eltsize <1) return NULL;
  
  struct header *chan;
  struct channel *channel;
  void *mem;

  channel=
    (struct channel *)
    mmap(NULL,
	 sizeof (struct channel),
	 PROT_WRITE|PROT_READ,
	 ((flags == CHANNEL_PROCESS_SHARED)?MAP_SHARED:MAP_PRIVATE)
	 |MAP_ANONYMOUS,-1,0);
  
  if(channel==MAP_FAILED) { return NULL; }

  if (flags != CHANNEL_PROCESS_ONECPY) {
    
    mem= 
      mmap(NULL,
	   (size == 0)?
	   sizeof(struct header):
	   sizeof (struct header) + size*eltsize,
	   PROT_WRITE|PROT_READ,
	   ((flags == CHANNEL_PROCESS_SHARED)?MAP_SHARED:MAP_PRIVATE)
	   |MAP_ANONYMOUS,-1,0);
    
    if(mem==MAP_FAILED) { printf ("failed2\n"); return NULL; }
    
    chan = (struct header *) mem;
    chan -> buffer = NULL;
    channel->mem= mem;

    chan -> incr = eltsize;
    chan -> size = size * eltsize;
    chan -> pos_deb = sizeof(struct header);
    chan -> pos_w = sizeof(struct header);

  } else { // si canal a une copie

    chan= malloc (sizeof(struct header));
    chan -> buffer= calloc (size, sizeof (void *));
    channel->mem= chan;

    chan -> incr = 1;
    chan -> size = size;
    chan -> pos_deb = 0;
    chan -> pos_w = 0;

  }

  sem_init (&chan->lock, (flags & CHANNEL_PROCESS_SHARED)?1:0, 1);
  
  sem_init (&chan->sem_a, (flags & CHANNEL_PROCESS_SHARED)?1:0, 0);
  sem_init (&chan->sem_c, (flags & CHANNEL_PROCESS_SHARED)?1:0, 0);
  sem_init (&chan->sem_d, (flags & CHANNEL_PROCESS_SHARED)?1:0, 0);

  chan -> pos_r = -1;
  chan -> eltsize = eltsize;
  chan -> waiter_r= 0;
  chan -> waiter_w= 0;
  chan -> close = 0;

  chan -> addr_sync = NULL;
  chan -> flags= flags;
    
  return channel;
  
}

int
channel_send (struct channel *channel, const void *data) {

  if (channel == NULL || channel -> mem == NULL || data == NULL)
    return -1;

 
  struct header *chan= (struct header *) channel->mem;
  
  if (chan->size > 0) { // canaux asynchrone

    sem_wait (&chan->lock);
    //////////////////////

    // test si le canal est fermé
    if (chan -> close == 1)
      { sem_post (&chan->lock); return 0; }

    // attend jusqu'a qu'une place se soit libéré
    while (chan->pos_w == -1)
      { chan->waiter_w ++;
	sem_post (&chan->lock);
	sem_wait (&chan->sem_c);
	sem_wait (&chan->lock);

	if (chan -> close == 1)
	  { sem_post (&chan->lock); return 0; }
	
      }

    if (chan->flags == CHANNEL_PROCESS_ONECPY)
      { ASYNC_ONECPY_SEND (chan, data); }
    else
      { ASYNC_SEND (chan, channel, data); }

    chan->pos_w = chan->pos_w + chan-> incr;
    
    if(chan->pos_r == -1) {					
      chan->pos_r=chan->pos_w - chan-> incr;			
      while (chan->waiter_r > 0)					
	{ chan->waiter_r --; sem_post (&chan->sem_d); }		
    }								
    if (chan->pos_deb+chan->size == chan->pos_w)
      chan->pos_w = chan->pos_deb;			
    if (chan->pos_w == chan->pos_r)
      chan->pos_w = -1;
    
    ////////////////////////
    sem_post (&chan->lock);
    
  } else { // canaux synchrone

    
    if (chan -> close == 1)
      { return 0; }
    
    sem_wait(&chan->sem_a);
    ///////////////////////

    if (chan -> close == 1)
      { sem_post(&chan->sem_a); return 0; }
    
    memcpy(chan->addr_sync, data, chan->eltsize);
    
    ///////////////////////////
    sem_post (&chan->sem_c);
    
  }
    
  return 1;
  
}


int
channel_recv (struct channel *channel, void *data) {

  if (channel == NULL || channel -> mem == NULL || data == NULL)
    return -1;

  struct header *chan= (struct header *) channel->mem;
  
  if (chan->size > 0) { // canaux asynchrone
  
    sem_wait (&chan->lock);
    ///////////////////////

    // test si la canal est fermé
    if (chan -> close == 1 && chan -> pos_r == -1)
      { sem_post (&chan->lock); return 0; }


    // attend jusqu'a qu'une donné soit écrite
    while (chan -> pos_r == -1)
      {
	chan -> waiter_r ++;
	sem_post (&chan->lock);
	sem_wait (&chan->sem_d);
	sem_wait (&chan->lock);

	if (chan -> close == 1 && chan -> pos_r == -1)
	  { sem_post (&chan->lock); return 0; }
	
      }
    
    if (chan-> flags == CHANNEL_PROCESS_ONECPY)
      { ASYNC_ONECPY_RECV (chan, data); }
    else
      { ASYNC_RECV (chan, channel, data); }

    chan->pos_r = chan->pos_r + chan -> incr;

    if(chan->pos_w == -1) {
      chan->pos_w=chan->pos_r - chan->incr;
      while (chan->waiter_w > 0)
	{ chan->waiter_w --; sem_post (&chan->sem_c); }
    }
    if (chan->pos_deb+chan->size == chan->pos_r)
      chan->pos_r = chan->pos_deb;
    if (chan->pos_w == chan->pos_r)
      chan->pos_r = -1;	
    
    ////////////////////////
    sem_post (&chan->lock);
    
  } else { // canaux synchrones

    sem_wait(&chan->lock);
    //////////////////////

    if (chan -> close == 1)
      { sem_post(&chan->lock); return 0; }
    
    chan->addr_sync=data;

    ///////////////////////
    sem_post(&chan->sem_a);
    sem_wait(&chan->sem_c);
    sem_post(&chan->lock);
    
  }
    
  return 1;
  
}



int
channel_close(struct channel *channel) {

  if (channel == NULL || channel -> mem == NULL)
    return -1;

  struct header *chan= (struct header *) channel->mem;

  if (chan -> size > 0) { // canaux asynchrone
    
    sem_wait (&chan->lock);
    ///////////////////////

    // fermeture du canal
    chan -> close = 1 ;

    // reveil de tous les lecteurs
    while (chan->waiter_r > 0)			
      { chan->waiter_r --; sem_post (&chan->sem_d); }

    // reveil de tous les ecrivains
    while (chan->waiter_w > 0)			
      { chan->waiter_w --; sem_post (&chan->sem_c); }
    
    ////////////////////////
    sem_post (&chan->lock);

  } else { // canaux synchrone

    // fermeture du canal
    chan -> close = 1 ;

    // si un ecrivain est resté endormi, on le reveille
    sem_post (&chan->sem_a);

    // si un lecteur est resté endormi, on le reveille
    sem_post (&chan->sem_c);

  }
    
  return 1;

}

void
channel_destroy(struct channel *channel) {

  struct header *hd;
  
  if (channel != NULL) {
    if (channel -> mem != NULL) {
      hd= (struct header *) channel -> mem;
      if (hd -> buffer != NULL)
        free (hd -> buffer);
      free (channel -> mem);
    }
    free (channel);
  }
  
}
