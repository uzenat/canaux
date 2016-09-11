# Canaux synchrone et asynchrone

Utilisation
==

Création d'un channel :
--

struct channel *
channel_create (int eltsize, int size, int flags)

struct channel *
channel_unrelated_create (int eltsize, int size, char *path)

valeurs flags :

0 : canal simple
CHANNEL_PROCESS_SHARED: canal global
CHANNEL_PROCESS_ONECPY: canal à une copie

si size vaut 0, alors on est en mode synchrone

Envoi de données
--

struct channel *
channel_send (struct channel *channel, void *data)

Réception de données
--

struct channel *
channel_recv (struct channel *channel, void *data)


Compilation
--

Pour compiler le fichier channel.c, on doit obligatoirement utiliser
les options -pthread et -lrt de gcc.


Exemple
==

On dispose de deux exemples:


Mandelbrot
--

Compilation:
gcc -g -O3 -ffast-math -Wall -pthread
‘pkg-config --cflags gtk+-3.0‘
mandelbrot.c channel.c
‘pkg-config --libs gtk+-3.0‘ -lm -lrt -DMODE=d

avec
d=0 pour canal simple
d=1 pour canal non apparenté
d=2 pour canal à une copie
d=3 pour pipe

pour l'executer on lance l'éxecutable avec l'option -s pour les canaux synchrones :

./a.out [ -s ]


Producteur consommateur
--

compilation:
gcc -pthread channel.c producteur_consommateur.c -lrt

pour éxecuter le programme on tapera:

./executable
--writer <entier>
--reader <entier>
--data <entier>
[ --pipe | --async | --async1 | sync ]


