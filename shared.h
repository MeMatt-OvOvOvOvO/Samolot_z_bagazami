#ifndef SHARED_H
#define SHARED_H

#include <semaphore.h>
#include <pthread.h>

/* Liczba stanowisk bezpieczeństwa */
#define SECURITY_STATIONS 3

struct global_data {
    int total_passengers;
    int generated_count;
    int finished_passengers;
    int is_simulation_active;

    int baggage_limit;
    int stairs_capacity;

    int takeoff_time;
    int plane_capacity;
    int people_in_plane;

    int plane_start_earlier; // 0 = nie, 1 = tak
    int plane_ready;
    int stairs_occupancy;
    int stop_generating;

    int plane_sum_of_luggage;
    int plane_luggage_capacity;

    /* Flaga: 0 = można wsiadać do bieżącego samolotu
     *        1 = samolot startuje, spóźnieni czekają na nowy
     */
    int plane_in_flight;

    sem_t *baggage_check_sem;
    sem_t *stairs_sem;

    /* TABLICA WSKAŹNIKÓW do semaforów nazwanych (3 stanowiska).
     * Każdy semafor ma początkową wartość = 2. */
    sem_t *security_sem[SECURITY_STATIONS];

    int station_gender[SECURITY_STATIONS]; // -1=puste, 0=m,1=f
    int station_occupancy[SECURITY_STATIONS];

    pthread_mutex_t station_mutex;
    pthread_mutex_t g_data_mutex;
};

/* Deklaracja jednej "global_data" */
extern struct global_data g_data;

int get_positive_int(const char *prompt);

void setup_signals(void);

/* Funkcja do ignorowania ENOENT w sem_unlink() */
void safe_sem_unlink(const char *name);

void enqueue_hall(int passenger_id, int is_vip, int bag_weight);

typedef struct hall_node {
    int passenger_id;
    int is_vip;
    int bag_weight;
    char sem_name[64]; // nazwa semafora nazwanego do boardingu
    sem_t *board_sem; // semafor do odblokowania pasażera
    struct hall_node *next;
} hall_node;

/* Dwie kolejki: VIP i normal */
static hall_node *vip_head = NULL, *vip_tail = NULL;
static hall_node *normal_head = NULL, *normal_tail = NULL;

static pthread_mutex_t hall_mutex = PTHREAD_MUTEX_INITIALIZER;

hall_node* dequeue_hall(void);

void print_hall_queues(void);

int is_passenger_in_hall(int pid);

#endif