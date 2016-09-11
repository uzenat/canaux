#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <complex.h>
#include <unistd.h>
#include <pthread.h>
#include <gtk/gtk.h>

#define ITERATIONS 1000
#define QSIZE 1000
#define COUNT 1000

#ifndef MODE
#define MODE 0
#endif

#if MODE == 0 || MODE == 1 || MODE == 2
#include "channel.h"
#endif


/* Voir la fonction toc ci-dessous. */

double scale = 1024 / 4.0;
int dx, dy;

/* La structure qui contient une requête du programme principal. */

struct mandel_request {
    short int x, y;
    short int count;
};

/* La réponse d'un travailleur. */

struct mandel_reply {
    short int x, y;
    short int count;
    short int data[COUNT];
};

struct twochans {

#if MODE == 0 || MODE == 1 || MODE == 2
    struct channel *one, *two;
#else
  int one[2];
  int two[2];
#endif
};

/* Le gros du boulot. */

static int
mandel(double complex c)
{
    double complex z = 0.0;
    int i = 0;
    while(i < 1000 && creal(z) * creal(z) + cimag(z) * cimag(z) <= 4.0) {
        z = z * z + c;
        i++;
    }
    return i;
}

/* Associe à une paire de coordonnées un point du plan complexe. */

static double complex
toc(int x, int y)
{
    return ((x - dx) + I * (y - dy)) / scale;
}

static int
min(int a, int b)
{
    return a <= b ? a : b;
}

/* Le travailleur. */

static void *
mandel_thread(void *arg)
{
    struct twochans chans = *(struct twochans*)arg;
    while(1) {
      
#if MODE == 2
        struct mandel_request *req=
	  malloc (sizeof(struct mandel_request));
        struct mandel_reply *rep=
	  malloc (sizeof(struct mandel_reply));
#else      
        struct mandel_request req;
        struct mandel_reply rep;
#endif
	int rc;
	int i;

#if MODE == 0 || MODE == 1
        rc = channel_recv(chans.one, &req);
        if(rc <= 0) {
	  channel_close(chans.two);
	  return NULL;
        }
#elif MODE == 2
	rc = channel_recv(chans.one, req);
        if(rc <= 0) {
	  channel_close(chans.two);
	  return NULL;
        }
#else
	rc = read (chans.one[0], &req, sizeof (struct mandel_request));
        if(rc <= 0) {
	  return NULL;
        }
#endif

#if MODE == 2
	rep->x = req->x;
        rep->y = req->y;
        rep->count = req->count;
        for(i = 0; i < req->count; i++) {
	  rep->data[i] = mandel(toc(req->x + i, rep->y));
        }
#else
        rep.x = req.x;
        rep.y = req.y;
        rep.count = req.count;
        for(i = 0; i < req.count; i++) {
	  rep.data[i] = mandel(toc(req.x + i, rep.y));
        }
#endif

#if MODE == 0 || MODE == 1
	rc = channel_send(chans.two, &rep);
        if(rc < 0) {
            channel_close(chans.two);
            return NULL;
        }
#elif MODE == 2
	rc = channel_send(chans.two, rep);
        if(rc < 0) {
            channel_close(chans.two);
            return NULL;
        }
#else
	rc = write (chans.two[1], &rep, sizeof (struct mandel_reply));
        if(rc < 0) {
            return NULL;
        }
#endif
	
    }
}

/* Convertit un nombre d'itérations en une couleur, en format RGB24. */

static unsigned int
torgb(int n)
{
    unsigned char r, g, b;

    if(n < 256)
        n *= 2;
    else
        n += 256;

    if(n < 256) {
        r = 255 - n;
        g = 0;
        b = n;
    } else if(n < 512) {
        r = n - 256;
        g = 511 - n;
        b = 0;
    } else if(n < 768) {
        r = 0;
        g = n - 512;
        b = 767 - n;
    } else if(n < 1024) {
        g = 255;
        r = b = n - 768;
    } else {
        r = g = b = 255;
    }

    return r << 16 | g << 8 | b;
}

/* Lit les réponses du travailleur et les met à l'écran.
   repcount est le nombre de réponses à lire. */

static void
paintit(struct twochans *chans, cairo_t *cr, int repcount)
{
    cairo_surface_t *surface;
    unsigned *data;
    unsigned rgb;
    int i, j;

    for(i = 0; i < repcount; i++) {

#if MODE == 2
      struct mandel_reply *rep=
	malloc (sizeof(struct mandel_reply));
#else
      struct mandel_reply rep;
#endif

      int rc;

#if MODE == 0 || MODE == 1
        rc = channel_recv(chans->two, &rep);
        if(rc <= 0) {
            perror("channel_recv");
            return;
        }
#elif MODE == 2
	rc = channel_recv(chans->two, rep);
        if(rc <= 0) {
	  perror("channel_recv");
	  return;
        }	
#else
	rc = read (chans->two[0], &rep, sizeof (struct mandel_reply));
        if(rc <= 0) {
	  perror("channel_recv");
	  return;
        }
#endif
	
        /* Avec les toolkits modernes, on ne peut plus travailler pixel par
           pixel.  Pas grave, on va travailler en mémoire principale. */
        surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
#if MODE == 2
					     rep->count
#else
					     rep.count
#endif
					     , 1);
	
        data = (unsigned*)cairo_image_surface_get_data(surface);
        for(j = 0; j <
#if MODE == 2
	      rep->count
#else
	      rep.count
#endif
	      ; j++) {
	  
            int n =
#if MODE == 2
	      rep->data[j]
#else
	      rep.data[j]
#endif
	      ;
	    
            if(n >= ITERATIONS) {
                rgb = 0;
            } else {
                rgb = torgb(n);
            }
            data[j] = rgb;
        }
        cairo_surface_mark_dirty(surface);
	
#if MODE == 2
	cairo_set_source_surface(cr, surface, rep->x, rep->y);
#else
	cairo_set_source_surface(cr, surface, rep.x, rep.y);
#endif
	
	cairo_paint(cr);
        cairo_surface_destroy(surface);
    }
}

/* Le toolkit nous demande de nous redessiner. */

gboolean
draw_callback (GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct twochans *chans = (struct twochans*)data;
    double x1, y1, x2, y2;
    int repcount = 0;
    int rc;
    struct timespec t0, t1;
    int i, j;
    
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for(j = y1; j <= y2; j++) {
        for(i = x1; i <= x2; i += COUNT) {
#if MODE == 2
	  struct mandel_request *req=
	    malloc (sizeof(struct mandel_request));
#else
	  struct mandel_request req;
#endif

#if MODE == 2
	  req->x = i;
	  req->y = j;
	  req->count = min(COUNT, x2 - i);
#else
	  req.x = i;
	  req.y = j;
	  req.count = min(COUNT, x2 - i);
#endif
	    
#if MODE == 0 || MODE == 1
            rc = channel_send(chans->one, &req);
            if(rc <= 0) {
	      perror("channel_send");
                /* Pas de bonne façon de gérer l'erreur sans deadlock. */
	      continue;
            }
#elif MODE == 2
	    rc = channel_send(chans->one, req);
            if(rc <= 0) {
	      perror("channel_send");
	      /* Pas de bonne façon de gérer l'erreur sans deadlock. */
	      continue;
            }
#else
	    rc =
	      write(chans->one[1],&req,sizeof (struct mandel_request));
            if(rc <= 0) {
	      perror("channel_send");
	      /* Pas de bonne façon de gérer l'erreur sans deadlock. */
	      continue;
            }
#endif
	    
            repcount++;
            if(repcount >= QSIZE) {
                /* Un canal bloque lorsqu'il est plein.  Il faut donc
                   vider les canaux à temps pour éviter un deadlock. */
                paintit(chans, cr, repcount);
                repcount = 0;
            }
        }
    }
    /* On vide ce qui reste dans les canaux. */
    paintit(chans, cr, repcount);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    printf("Repaint done in %.2lfs\n",
           ((double)t1.tv_sec - t0.tv_sec) +
           ((double)t1.tv_nsec - t0.tv_nsec) / 1.0E9);
    return FALSE;
}

/* L'utilisateur a fait clic. */

gboolean
button_callback(GtkWidget *widget, GdkEventButton *event)
{
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double dscale = 1.0;

    /* Le bouton 1 agrandit, le bouton 3 réduit. */

    if(event->button == 1)
        dscale = sqrt(2.0);
    else if(event->button == 3)
        dscale = 1.0 / sqrt(2.0);
    scale *= dscale;

    /* On met la location du clic au centre. */

    dx = width / 2 + (dx - event->x) * dscale;
    dy = height / 2 + (dy - event->y) * dscale;

    gtk_widget_queue_draw(widget);
    return TRUE;
}

int main(int argc, char **argv)
{
    GtkWidget* window;
    GtkWidget* canvas;
    struct twochans chans;
    int numthreads = 0;

#if MODE == 0 || MODE == 1 || MODE == 2
    int synchronous = 0;
#endif
    
    int rc;
    const char *usage = "./mandelbrot [-n numthreads] [-s]";
    int i;
    
    gtk_init(&argc, &argv);

    while(1) {
        int opt = getopt(argc, argv, "n:s");
        if(opt < 0)
            break;

        switch(opt) {
        case 'n':
            numthreads = atoi(optarg);
            break;
        case 's':
#if MODE == 0 || MODE == 1
            synchronous = 1;
            break;
#else
	    fprintf (stderr, "Not with pipe or onecpy\n");
	    exit (1);
#endif
	    
        default:
            fprintf(stderr, "%s\n", usage);
            exit(1);
        }
    }

    if(optind < argc) {
        fprintf(stderr, "%s\n", usage);
        exit(1);
    }

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 512);
    dx = 512;
    dy = 256;

    canvas = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(window), canvas);

    
#if MODE == 0
    chans.one =
      channel_create(sizeof(struct mandel_request), QSIZE, 0);
    if(chans.one == NULL) {
      perror("channel_create");
      exit(1);
    }
#elif MODE == 1
    chans.one =
      channel_unrelated_create
      (sizeof(struct mandel_request), QSIZE, "share_one");
    if(chans.one == NULL) {
      perror("channel_create");
      exit(1);
    }
#elif MODE == 2
    chans.one =
      channel_create
      (sizeof(struct mandel_request), QSIZE, CHANNEL_PROCESS_ONECPY);
    if(chans.one == NULL) {
      perror("channel_create");
      exit(1);
    }   
#else
    rc= pipe (chans.one); 
    if(rc < 0) {
      perror("channel_create");
      exit(1);
    }
#endif
    
    

#if MODE == 0
    chans.two =
      channel_create(sizeof(struct mandel_reply),
                     synchronous ? 0 : QSIZE, 0);
    if(chans.two == NULL) {
        perror("channel_create");
        exit(1);
    }
#elif MODE == 1
    chans.two =
      channel_unrelated_create
      (sizeof(struct mandel_reply),
       synchronous ? 0 : QSIZE, "share_two");
    if(chans.two == NULL) {
      perror("channel_create");
      exit(1);
    }
#elif MODE == 2
    chans.two =
      channel_create
      (sizeof(struct mandel_reply),
       synchronous ? 0 : QSIZE, CHANNEL_PROCESS_ONECPY);
    if(chans.two == NULL) {
      perror("channel_create");
      exit(1);
    }  
#else
    pipe (chans.two);
    if(chans.two < 0) {
      perror("channel_create");
      exit(1);
    }
#endif
      
    

    if(numthreads <= 0)
        numthreads = sysconf(_SC_NPROCESSORS_ONLN);
    if(numthreads <= 0) {
        perror("sysconf(_SC_NPROCESSORS_ONLN)");
        exit(1);
    }
    printf("Running %d worker threads.\n", numthreads);

    for(i = 0; i < numthreads; i++) {
        pthread_t t;
        rc = pthread_create(&t, NULL, mandel_thread, &chans);
        if(rc != 0) {
            errno = rc;
            perror("pthread_create");
            exit(1);
        }
        /* On se synchronise à l'aide des canaux, pas besoin de join. */
        pthread_detach(t);
    }

    g_signal_connect(window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect(canvas, "draw", G_CALLBACK(draw_callback), &chans);
    g_signal_connect(window, "button_press_event",
                     G_CALLBACK(button_callback), NULL);
    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}
