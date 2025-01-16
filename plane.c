#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "shared.h"
#include "plane.h"

void *plane_thread(void *arg)
{
    (void)arg;
    printf("[PLANE] Start - wielokrotne loty.\n");

    int flight_no = 0;

    while (1) {
        // Sprawdzamy, czy wszyscy pasażerowie skończyli
        pthread_mutex_lock(&g_data.g_data_mutex);
        int finished = g_data.finished_passengers;
        int total = g_data.total_passengers;
        int active = g_data.is_simulation_active;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (finished >= total || !active) {
            printf("[PLANE] Nie ma potrzeby kolejnego lotu (finished=%d/%d, active=%d).\n",
                   finished, total, active);
            break;
        }

        flight_no++;
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.people_in_plane = 0;
        g_data.plane_in_flight = 0;   // samolot dostępny (drzwi otwarte)
        g_data.plane_start_earlier = 0;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[PLANE] >>> Samolot #%d czeka max %d sek lub do zapełnienia.\n",
               flight_no, g_data.takeoff_time);

        int elapsed = 0;
        int step = 1;

        while (1) {
            /* BOARDOWANIE PASAŻERÓW Z HOLU (o ile jest miejsce) */
            pthread_mutex_lock(&g_data.g_data_mutex);
            int plane_now = g_data.people_in_plane;
            int capacity = g_data.plane_capacity;
            int finished2 = g_data.finished_passengers;
            int tot2 = g_data.total_passengers;
            int active2 = g_data.is_simulation_active;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            if (!active2) {
                printf("[PLANE] (Lot %d) Symulacja nieaktywna.\n", flight_no);
                goto plane_end;
            }

            if (plane_now < capacity) {
                // Pobieramy jednego pasażera z holu
                hall_node *hn = dequeue_hall();
                if (hn) {
                    // Odblokowujemy semafor board_sem pasażera,
                    // co pozwoli mu wyjść z sem_wait w passenger_thread
                    sem_post(hn->board_sem);
                    // Możemy zamknąć i usunąć semafor od razu
                    // LUB pozwolić pasażerowi zrobić to samemu:
                    sem_close(hn->board_sem);
                    safe_sem_unlink(hn->sem_name);
                    free(hn);

                    // Po sem_post pasażer dopiero w wątku passenger_thread
                    // wykona enter_stairs_and_plane() i people_in_plane++.
                    // Potrzebujemy krótkiego odczekania, by inkrement mógł się wykonać:
                    usleep(100000); // 0.1 sek

                    // Ponownie sprawdzamy plane_now ...
                }
                else {
                    // Kolejka pusta => nikt nie czeka w holu
                    // Ale może ktoś jeszcze nie dotarł do holu
                    // -> czekamy normalnie
                }
            }

            /* Sprawdzamy warunki startu */
            pthread_mutex_lock(&g_data.g_data_mutex);
            plane_now = g_data.people_in_plane;
            int start_earlier = g_data.plane_start_earlier;
            finished2 = g_data.finished_passengers;
            active2 = g_data.is_simulation_active;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            // 1) Pełny?
            if (plane_now >= capacity) {
                printf("[PLANE] (Lot %d) Pełny (%d/%d). Start!\n",
                       flight_no, plane_now, capacity);
                break;
            }

            // 2) Sygnał start_earlier
            if (start_earlier == 1) {
                printf("[PLANE] (Lot %d) [SIGUSR1] Startujemy wcześniej!\n", flight_no);
                pthread_mutex_lock(&g_data.g_data_mutex);
                g_data.plane_start_earlier = 0;
                pthread_mutex_unlock(&g_data.g_data_mutex);
                break;
            }

            // Jeśli minął T1 -> start
            if (elapsed >= g_data.takeoff_time) {
                printf("[PLANE] (Lot %d) Minęło %d sek, zaczynamy start. Czekamy na ostatnich pasazerow!\n",
                       flight_no, elapsed);
                break;
            }

            // 4) Wszyscy skończyli, a w samolocie 0?
            if (finished2 >= tot2) {
                printf("[PLANE] (Lot %d) Wszyscy (%d/%d) skończyli, 0 pasażerów -> koniec.\n",
                       flight_no, finished2, tot2);
                goto plane_end;
            }

            // Odczekujemy 1 sek przed ponowną iteracją
            sleep(step);
            elapsed += step;
        }

        // Samolot startuje -> zamykamy "drzwi"
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_in_flight = 1;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        sleep(2);

        pthread_mutex_lock(&g_data.g_data_mutex);
        int plane_final = g_data.people_in_plane;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (plane_final == 0) {
            printf("[PLANE] (Lot %d) 0 pasażerów -> rezygnuję z lotu.\n", flight_no);
            break;
        }

        printf("[PLANE] (Lot %d) Odlatuję z %d pasażerami.\n", flight_no, plane_final);
        sleep(2);
        pthread_mutex_lock(&g_data.g_data_mutex);
		g_data.finished_passengers += plane_final;
		int fin_now = g_data.finished_passengers;
		int tot = g_data.total_passengers;
		g_data.people_in_plane = 0;  // opróżniamy samolot
		pthread_mutex_unlock(&g_data.g_data_mutex);

		printf("[PLANE] (Lot %d) Wylądowaliśmy. %d pasażerów doleciało. Samolot wraca (finished=%d/%d).\n",
       flight_no, plane_final, fin_now, tot);
        sleep(2);
        printf("[PLANE] (Lot %d) Wróciłem.\n", flight_no);
    }

plane_end:
    printf("[PLANE] Kończę wątek samolotu - nie będzie więcej lotów.\n");
    pthread_exit(NULL);
}