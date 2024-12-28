/*******************************************************
 * Program przyjmuje jeden parametr:
 *   ./airport_sim <liczba_pasazerow_do_wygenerowania>
 * Kompilacja:
 *   gcc -Wall -o airport_sim airport_sim.c -pthread
 *******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

struct {
    int total_passengers;
    int generated_count;
    int is_simulation_active;
} g_data;

pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Watek Dyspozytora (Wiezy Kontroli) */
void *dispatcher_thread(void *arg);

/* Watek Samolotu (Kapitan) */
void *plane_thread(void *arg);

/* Watek generatora pasazerow - tworzy watki pasazerow */
void *passenger_generator_thread(void *arg);

/* Kazdy Pasazer tez bedzie watkiem */
void *passenger_thread(void *arg);

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uzycie: %s <liczba_pasażerów>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Parsujemy liczbe pasazerow z argumentu. */
    g_data.total_passengers = atoi(argv[1]);
    if (g_data.total_passengers <= 0) {
        fprintf(stderr, "Niepoprawna liczba pasazerow: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    g_data.generated_count = 0;
    g_data.is_simulation_active = 1;

    /* Inicjujemy losowosc */
    srand(time(NULL));

    printf("[MAIN] Start symulacji. Docelowo wygenerujemy %d pasazerow.\n",
           g_data.total_passengers);

    /* Tworzymy watki: dispatcher, plane, passenger_generator */
    pthread_t th_dispatcher, th_plane, th_generator;

    if (pthread_create(&th_dispatcher, NULL, dispatcher_thread, NULL) != 0) {
        perror("pthread_create(dispatcher) error");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&th_plane, NULL, plane_thread, NULL) != 0) {
        perror("pthread_create(plane) error");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&th_generator, NULL, passenger_generator_thread, NULL) != 0) {
        perror("pthread_create(generator) error");
        exit(EXIT_FAILURE);
    }

    if (pthread_join(th_generator, NULL) != 0) {
        perror("pthread_join(generator) error");
        exit(EXIT_FAILURE);
    }
    if (pthread_join(th_plane, NULL) != 0) {
        perror("pthread_join(plane) error");
        exit(EXIT_FAILURE);
    }
    if (pthread_join(th_dispatcher, NULL) != 0) {
        perror("pthread_join(dispatcher) error");
        exit(EXIT_FAILURE);
    }

    printf("[MAIN] Symulacja zakonczona.\n");

    return EXIT_SUCCESS;
}

void *dispatcher_thread(void *arg)
{
    (void) arg;

    printf("[DISPATCHER] Startuje.\n");

    while (1) {
        sleep(3);

        pthread_mutex_lock(&g_data_mutex);
        int still_active = g_data.is_simulation_active;
        int gen_count = g_data.generated_count;
        pthread_mutex_unlock(&g_data_mutex);

        printf("[DISPATCHER] Raport: wygenerowano %d / %d pasazerow.\n",
               gen_count, g_data.total_passengers);

        if (!still_active) {
            printf("[DISPATCHER] Koncze prace (is_simulation_active=0).\n");
            break;
        }
    }

    pthread_exit(NULL);
}

void *plane_thread(void *arg)
{
    (void) arg;

    printf("[PLANE] Startuje.\n");

    while (1) {
        sleep(2);

        pthread_mutex_lock(&g_data_mutex);
        int still_active = g_data.is_simulation_active;
        pthread_mutex_unlock(&g_data_mutex);

        if (!still_active) {
            printf("[PLANE] Koncze prace (is_simulation_active=0).\n");
            break;
        }

    }

    pthread_exit(NULL);
}

void *passenger_generator_thread(void *arg)
{
    (void) arg;

    printf("[GENERATOR] Startuje. Bede tworzyl pasazerow.\n");

    while (1) {
        pthread_mutex_lock(&g_data_mutex);
        int current_count = g_data.generated_count;
        int max_count = g_data.total_passengers;
        pthread_mutex_unlock(&g_data_mutex);

        if (current_count >= max_count) {
            printf("[GENERATOR] Osiagnieto limit %d pasazerow.\n", max_count);

            pthread_mutex_lock(&g_data_mutex);
            g_data.is_simulation_active = 0;
            pthread_mutex_unlock(&g_data_mutex);

            break;
        }

        /* Losowo "odczekaj" przed stworzeniem kolejnego pasazera */
        int wait_time = rand() % 2 + 1;
        sleep(wait_time);

        /* Tworzymy nowy watek pasazera */
        pthread_t th_passenger;
        int *passenger_id = malloc(sizeof(int));
        if (!passenger_id) {
            perror("malloc() error");
            continue;
        }

        *passenger_id = current_count + 1;

        if (pthread_create(&th_passenger, NULL, passenger_thread, passenger_id) != 0) {
            perror("pthread_create(passenger) error");
            free(passenger_id);
            continue;
        }

        if (pthread_detach(th_passenger) != 0) {
            perror("pthread_detach(passenger) error");
        }

        pthread_mutex_lock(&g_data_mutex);
        g_data.generated_count++;
        pthread_mutex_unlock(&g_data_mutex);
    }

    printf("[GENERATOR] Koncze prace.\n");
    pthread_exit(NULL);
}

void *passenger_thread(void *arg)
{
    int my_id = *((int *)arg);
    free(arg); // zwalniamy pamiec zaalokowana w generatorze

    int bag_weight = rand() % 10 + 1; // 1-10 kg
    int is_vip = (rand() % 5 == 0) ? 1 : 0; // co piaty VIP
    int gender = (rand() % 2); // 0 = mezczyzna, 1 = kobieta

    printf("[PASSENGER %d] Jestem watkiem pasazera (bagaz=%d kg, VIP=%d, gender=%d)\n",
           my_id, bag_weight, is_vip, gender);

    int processing_time = rand() % 3 + 1;
    sleep(processing_time);

    printf("[PASSENGER %d] Koncze (odbylem podroz / zrezygnowalem / itp.)\n", my_id);

    pthread_exit(NULL);
}