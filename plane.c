#include <stdio.h>
#include <unistd.h>
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

        // "Nowy samolot" - zerujemy people_in_plane i plane_in_flight=0
        flight_no++;
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.people_in_plane = 0;
        g_data.plane_in_flight = 0; // otwieramy drzwi - można wsiadać
        g_data.plane_start_earlier = 0; // reset sygnału (na kolejny lot)
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[PLANE] >>> Samolot #%d czeka max %d sek lub do zapełnienia.\n",
               flight_no, g_data.takeoff_time);

        int elapsed = 0;
        int step = 1;

        while (1) {
            sleep(step);
            elapsed += step;

            pthread_mutex_lock(&g_data.g_data_mutex);
            int plane_now = g_data.people_in_plane;
            int capacity = g_data.plane_capacity;
            int fin2 = g_data.finished_passengers;
            int tot2 = g_data.total_passengers;
            int active2 = g_data.is_simulation_active;
            int start_earlier_flag = g_data.plane_start_earlier;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            // Jeżeli symulacja się skończyła
            if (!active2) {
                printf("[PLANE] (Lot %d) Symulacja nieaktywna.\n", flight_no);
                goto plane_end; // break z pętli while(1) -> break z pętli głównej
            }

            // Jeśli samolot pełny -> start
            if (plane_now >= capacity) {
                printf("[PLANE] (Lot %d) Samolot pełny (%d/%d). Start wcześniej!\n",
                       flight_no, plane_now, capacity);
                break;
            }

            if (start_earlier_flag == 1) {
                printf("[PLANE] [SIGUSR1] Startujemy wcześniej!\n");
                // zresetuj flagę
                pthread_mutex_lock(&g_data.g_data_mutex);
                g_data.plane_start_earlier = 0;
                pthread_mutex_unlock(&g_data.g_data_mutex);
                break;
            }

            // Jeśli minął T1 -> start
            if (elapsed >= g_data.takeoff_time) {
                printf("[PLANE] (Lot %d) Minęło %d sek, startujemy!\n",
                       flight_no, elapsed);
                break;
            }

            // A może wszyscy pasażerowie już skończyli i wtedy moze pdleciec szybciej
            if (fin2 >= tot2) {
                printf("[PLANE] (Lot %d) Wszyscy pasażerowie (%d/%d) -> start z kim jest.\n",
                       flight_no, fin2, tot2);
                break;
            }
        }

        // Gdy zdecydowaliśmy o starcie -> blokujemy wejście do tego samolotu
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_in_flight = 1; // samolot "zamknięty" - spóźnialscy czekają na nowy
        pthread_mutex_unlock(&g_data.g_data_mutex);

        // Dajemy 2 sek. aby "zakończyć" ewentualne wejścia w trakcie
        // (ci, co wejdą w tym momencie, zobaczą plane_in_flight=1 i poczekają)
        sleep(2);

        // Pobieramy finalną liczbę pasażerów
        pthread_mutex_lock(&g_data.g_data_mutex);
        int plane_final = g_data.people_in_plane;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[PLANE] (Lot %d) Odlatuję z %d pasażerami.\n", flight_no, plane_final);
        sleep(4); // symulacja lotu
        printf("[PLANE] (Lot %d) Wróciłem. Pasażerowie wysiadają (licznik zerujemy przy nast. locie).\n", flight_no);
    }

    plane_end:
        printf("[PLANE] Kończę wątek samolotu - nie będzie więcej lotów.\n");
    pthread_exit(NULL);
}