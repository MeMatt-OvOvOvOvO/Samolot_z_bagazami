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
        int plane_capacity = g_data.plane_capacity;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (finished >= total || !active) {
            printf("[PLANE] Nie ma potrzeby kolejnego lotu (finished=%d/%d, active=%d).\n",
                   finished, total, active);
            break;
        }

        flight_no++;
        int random_factor = 7 + (rand() % 4); // losowo 7..10
		int plane_luggage_capacity = plane_capacity * random_factor;
		int plane_sum_of_luggage = 0;
        int attempt_to_fit_again = 0;
        int delay = rand() % 4;  // 0..3
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_start_earlier = 0;
        g_data.plane_ready = delay;
        g_data.plane_sum_of_luggage = plane_sum_of_luggage;
    	g_data.plane_luggage_capacity = plane_luggage_capacity;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[PLANE] (Lot %d) Za chwilę boarding, ale czekam %d sek (plane_ready=%d).\n",
               flight_no, delay, delay);

        printf("[PLANE] (Lot %d) Maksymalna łączna waga bagażu = %d\n",
               flight_no, plane_luggage_capacity);

        sleep(delay);

        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.people_in_plane = 0;
        g_data.plane_in_flight = 0;   // samolot dostępny (drzwi otwarte)
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[PLANE] (Lot %d) Rozpoczynam boarding (po zakonczeniu opóźnienia %d sek) czeka max %d sek lub do zapełnienia..\n",
               flight_no, delay, g_data.takeoff_time);

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
    			hall_node *hn = dequeue_hall();
    			if (hn) {
        			int pid = hn->passenger_id;
        			int vip = hn->is_vip;
        			int bw = hn->bag_weight;

        			// SPRAWDZAMY BAGAŻ:
        			if (plane_sum_of_luggage + bw <= plane_luggage_capacity) {
            			printf("[PLANE] (Lot %d) Zapraszam pasażera %d (VIP=%d, bag=%d). ",
                   			flight_no, pid, vip, bw);

            			// Odblokowujemy go, by mógł enter_stairs_and_plane()
            			sem_post(hn->board_sem);
            			sem_close(hn->board_sem);
            			safe_sem_unlink(hn->sem_name);

            			free(hn);

            			// Krótkie usleep, by passenger zdążył people_in_plane++.
            			usleep(100000);

            			// reset attempt:
            			attempt_to_fit_again = 0;
        			}else {
            			// Nie mieści się
            			printf("[PLANE] (Lot %d) Pasażer %d bag=%d NIE mieści się. Suma=%d, limit=%d\n",
                   			flight_no, pid, bw, plane_sum_of_luggage, plane_luggage_capacity);

            			// Odkładamy pasażera do holu
            			enqueue_hall(pid, vip, bw);
            			free(hn);

            			if (attempt_to_fit_again == 0) {
                			// Pierwszy raz:
                			printf("[PLANE] (Lot %d) Jeszcze raz spróbuję wziąć pasażera.\n", flight_no);
                			attempt_to_fit_again = 1;
            			} else {
                			// Drugi pasażer też nie pasuje -> samolot odlatuje
                			printf("[PLANE] (Lot %d) Drugi pasażer też się nie mieści -> Samolot opóźniony, odlatuje!\n",
                       			flight_no);
                			// ustawić plane_in_flight=1, break z pętli boardingu
                			// (zapewne: break; i potem samolot startuje)
                			// ...
            			}
        			}
    			}else {
        			// hol pusty => break lub czekamy?
        			// ...
    			}
            }

            /* Sprawdzamy warunki startu */
            pthread_mutex_lock(&g_data.g_data_mutex);
            plane_now = g_data.people_in_plane;
            int start_earlier = g_data.plane_start_earlier;
            finished2 = g_data.finished_passengers;
            active2 = g_data.is_simulation_active;
        	plane_sum_of_luggage = g_data.plane_sum_of_luggage;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            // 1) Pełny?
            if (plane_now >= capacity) {
                printf("[PLANE] (Lot %d) Pełny (%d/%d). Bagaz %d/%d. Start!\n",
                       flight_no, plane_now, capacity, plane_sum_of_luggage, plane_luggage_capacity);
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
		sleep(2);
        // Samolot startuje -> zamykamy "drzwi"
        pthread_mutex_lock(&g_data.g_data_mutex);
        int plane_final = g_data.people_in_plane;
        g_data.plane_in_flight = 1;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (plane_final == 0) {
            printf("[PLANE] (Lot %d) 0 pasażerów -> rezygnuję z lotu.\n", flight_no);
            break;
        }

        printf("[PLANE] (Lot %d) Odlatuję z %d pasażerami. Jego bagaz to %d/%d.\n", flight_no, plane_final,
               plane_sum_of_luggage, plane_luggage_capacity);
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