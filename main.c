/*******************************************************
 *   ./airport_sim
 *       (Program zapyta o kolejne parametry.)
 * Kompilacja:
 *   gcc -Wall -o airport_sim main.c shared.c dispatcher.c plane.c passenger.c -pthread
 *******************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "shared.h"
#include "dispatcher.h"
#include "plane.h"
#include "passenger.h"

int main(void)
{
    printf("=== KONFIGURACJA SYMULACJI ===\n");

    g_data.total_passengers = get_positive_int("Podaj liczbę pasażerów: ");
    g_data.baggage_limit = get_positive_int("Podaj limit bagażu (kg): ");
    g_data.stairs_capacity = get_positive_int("Podaj pojemność schodów pasażerskich (K): ");
    g_data.takeoff_time = get_positive_int("Podaj czas (sek) po którym samolot odlatuje (T1): ");
    g_data.plane_capacity = get_positive_int("Podaj pojemność samolotu (P): ");

    g_data.generated_count = 0;
    g_data.finished_passengers = 0;
    g_data.passengers_rejected = 0;
    g_data.is_simulation_active = 1;
    g_data.people_in_plane = 0;
    g_data.plane_in_flight = 0;
    g_data.plane_start_earlier = 0;
    g_data.plane_ready = 0;
    g_data.stairs_occupancy = 0;
    g_data.stop_generating = 0;

    g_data.plane_sum_of_luggage = 0;
    g_data.plane_luggage_capacity = 0;

    setup_signals();

    /* Inicjujemy losowosc */
    srand(time(NULL));

    safe_sem_unlink("/baggage_check_sem"); // gdyby poprzedni semafor nie byl usuniety
    g_data.baggage_check_sem = sem_open("/baggage_check_sem", O_CREAT, 0666, 1);
    if (g_data.baggage_check_sem == SEM_FAILED) {
        perror("sem_open(baggage_check_sem)");
        return EXIT_FAILURE;
    }

    /* Semafor nazwany do schodow o pojemności K */
    safe_sem_unlink("/stairs_sem");
    g_data.stairs_sem = sem_open("/stairs_sem", O_CREAT, 0666, g_data.stairs_capacity);
    if (g_data.stairs_sem == SEM_FAILED) {
        perror("sem_open(stairs_sem)");
        return EXIT_FAILURE;
    }

    /* Semafory nazwane do 3 stanowisk bezpieczeństwa */
    for (int i = 0; i < SECURITY_STATIONS; i++) {
        char name[32];
        sprintf(name, "/security_sem_%d", i);
        safe_sem_unlink(name);

        g_data.security_sem[i] = sem_open(name, O_CREAT, 0666, 2);
        if (g_data.security_sem[i] == SEM_FAILED) {
            perror("sem_open(security_sem[i])");
            return EXIT_FAILURE;
        }
        g_data.station_gender[i] = -1;
        g_data.station_occupancy[i] = 0;
    }

    /* Inicjujemy muteksy */
    if (pthread_mutex_init(&g_data.station_mutex, NULL) != 0) {
        perror("pthread_mutex_init(station_mutex)");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&g_data.g_data_mutex, NULL) != 0) {
        perror("pthread_mutex_init(g_data_mutex)");
        exit(EXIT_FAILURE);
    }

    printf("[MAIN] Start symulacji. P=%d, limit=%d, stairs=%d, T1=%d, planeCap=%d\n",
           g_data.total_passengers, g_data.baggage_limit, g_data.stairs_capacity,
           g_data.takeoff_time, g_data.plane_capacity);

    /* Tworzymy wątki: dispatcher, plane, passenger_generator */
    pthread_t th_dispatcher, th_plane, th_generator;

    if (pthread_create(&th_dispatcher, NULL, dispatcher_thread, NULL) != 0) {
        perror("pthread_create(dispatcher)");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&th_plane, NULL, plane_thread, NULL) != 0) {
        perror("pthread_create(plane)");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&th_generator, NULL, passenger_generator_thread, NULL) != 0) {
        perror("pthread_create(generator)");
        exit(EXIT_FAILURE);
    }

    /* Czekamy na koniec wątków generator, plane, dispatcher */
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

    /* Sprzątanie semaforów i mutexów */
    if (sem_close(g_data.baggage_check_sem) != 0) {
        perror("sem_close(baggage_check_sem)");
    }
    safe_sem_unlink("/baggage_check_sem");

    if (sem_close(g_data.stairs_sem) != 0) {
        perror("sem_close(stairs_sem)");
    }
    safe_sem_unlink("/stairs_sem");

    for (int i = 0; i < SECURITY_STATIONS; i++) {
        if (sem_close(g_data.security_sem[i]) != 0) {
            perror("sem_close(security_sem[i])");
        }
        char name[32];
        sprintf(name, "/security_sem_%d", i);
        safe_sem_unlink(name);
    }

    pthread_mutex_destroy(&g_data.station_mutex);
    pthread_mutex_destroy(&g_data.g_data_mutex);

    printf("[MAIN] Symulacja zakończona.\n");
    return EXIT_SUCCESS;
}