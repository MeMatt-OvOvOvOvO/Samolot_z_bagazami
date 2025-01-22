#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "shared.h"
#include "plane.h"

void *plane_thread(void *arg)
{
    (void)arg;
    printf(ANSI_COLOR_BLUE "[PLANE] Start - wielokrotne loty.\n" ANSI_COLOR_RESET);

    int flight_no = 0;

    while (1) {
        /* Sprawdzenie stanu globalnego */
        pthread_mutex_lock(&g_data_mutex);
        int finished = g_data.finished_passengers;
        int total = g_data.total_passengers;
        int active = g_data.is_simulation_active;
        int plane_capacity = g_data.plane_capacity;
        pthread_mutex_unlock(&g_data_mutex);

        if (!active || finished >= total) {
            printf(ANSI_COLOR_BLUE "[PLANE] Nie ma potrzeby kolejnego lotu (finished=%d/%d, active=%d).\n" ANSI_COLOR_RESET,
                   finished, total, active);
            break;
        }

        /* Przygotowanie nowego lotu */
        flight_no++;
        int random_factor = 7 + (rand() % 4); // losowo z przedziału 7..10
        int plane_luggage_capacity = plane_capacity * random_factor;
        int plane_sum_of_luggage = 0;



        pthread_mutex_lock(&g_data_mutex);
        g_data.plane_sum_of_luggage = plane_sum_of_luggage;
        g_data.plane_luggage_capacity = plane_luggage_capacity;
        g_data.people_in_plane = 0;
        g_data.plane_in_flight = 0;  // lot otwarty, boarding dostępny
        pthread_mutex_unlock(&g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Nowy lot – limit bagażu = %d.\n" ANSI_COLOR_RESET,
               flight_no, plane_luggage_capacity);

        /******** BOARDING – wątek samolotu pobiera pasażerów z kolejki ********/
        while (1) {
            /* Sprawdzenie aktualnego stanu boardingu */
            pthread_mutex_lock(&g_data_mutex);
            int plane_now = g_data.people_in_plane;
            int capacity = g_data.plane_capacity;
            int active2 = g_data.is_simulation_active;
            int stop_gen = g_data.stop_generating;
            int start_earlier = g_data.plane_start_earlier;
            pthread_mutex_unlock(&g_data_mutex);

            if (!active2) {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Symulacja nieaktywna.\n" ANSI_COLOR_RESET, flight_no);
                goto plane_end;
            }

            if (start_earlier) {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Flaga startu wcześniej ustawiona. Kończę boarding i odlatam.\n" ANSI_COLOR_RESET, flight_no);
                break;
            }

            if (plane_now >= capacity) {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Samolot pełny (%d/%d). Boardowanie zakończone.\n" ANSI_COLOR_RESET,
                       flight_no, plane_now, capacity);
                break;
            }

            /* Pobieramy pasażera z kolejki holu */
            hall_node *hn = dequeue_hall();
            if (!hn) {
                /* Kolejka (hol) jest pusta.
                   Sprawdzamy stan kolejki i flagi stop_generating.
                */
                pthread_mutex_lock(&hall_mutex);
                int vip_empty = (vip_head == NULL);
                int normal_empty = (normal_head == NULL);
                pthread_mutex_unlock(&hall_mutex);

                pthread_mutex_lock(&g_data_mutex);
                plane_now = g_data.people_in_plane;
                stop_gen  = g_data.stop_generating;
                pthread_mutex_unlock(&g_data_mutex);

//                printf("[DEBUG] plane_thread: Kolejka holu pusta (VIP=%d, NOR=%d).\n", vip_empty, normal_empty);
                /* Jeśli kolejka jest pusta i mamy już pasażerów w samolocie,
                   lub jeśli generowanie zostało zatrzymane, wychodzimy z boardingu.
                */
                if ((vip_empty && normal_empty) && (plane_now > 0 || stop_gen)) {
                    printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Hol pusty – kończę boarding.\n" ANSI_COLOR_RESET, flight_no);
                    break;
                }
                else {
                    /* Jeśli kolejka pusty, ale nie mamy jeszcze wystarczająco pasażerów
                       oraz stop_generating nie jest ustawione, czekamy na sygnał.
                    */
                    pthread_mutex_lock(&hall_mutex);
                    pthread_cond_wait(&hall_not_empty_cond, &hall_mutex);
                    pthread_mutex_unlock(&hall_mutex);
                    continue;
                }
            }

            /* Mamy pasażera z holu – wykonujemy „boarding” */
            int pid = hn->passenger_id;
            int vip = hn->is_vip;
            int bw = hn->bag_weight;

            pthread_mutex_lock(&g_data_mutex);
            int current_sum = g_data.plane_sum_of_luggage;
            int plane_limit = g_data.plane_luggage_capacity;
            pthread_mutex_unlock(&g_data_mutex);

            if (current_sum + bw <= plane_limit) {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Zapraszam pasażera %d (VIP=%d, bag=%d).\n" ANSI_COLOR_RESET,
                       flight_no, pid, vip, bw);
                // Wybudzamy pasażera – wysyłamy sygnał boardingu
                sem_post(hn->board_sem);

                // Aktualizujemy licznik pasażerów oraz sumę bagażu
                pthread_mutex_lock(&g_data_mutex);
                g_data.people_in_plane++;
                g_data.plane_sum_of_luggage += bw;
                pthread_mutex_unlock(&g_data_mutex);

                if (sem_close(hn->board_sem) != 0) {
                    perror("sem_close(hn->board_sem)");
                }
                safe_sem_unlink(hn->sem_name);
                free(hn);
            } else {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Pasażer %d (bag=%d) NIE mieści się (sum=%d, limit=%d).\n" ANSI_COLOR_RESET,
                       flight_no, pid, bw, current_sum, plane_limit);
                // Jeśli pasażer nie mieści się wagowo, odkładamy go do kolejki
                enqueue_hall(pid, vip, bw);
                free(hn);
            }
        } // koniec pętli boardingu

        /******** Po zakończeniu boardingu ********/
        pthread_mutex_lock(&g_data_mutex);
        int ppl = g_data.people_in_plane;
        pthread_mutex_unlock(&g_data_mutex);

        if (ppl == 0) {
            printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) 0 pasażerów -> startuję z pustym samolotem.\n" ANSI_COLOR_RESET, flight_no);
        } else {
            printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Boardowanie zakończone: %d pasażerów wsiadło.\n" ANSI_COLOR_RESET, flight_no, ppl);
        }

        pthread_mutex_lock(&g_data_mutex);
        int takeoff_time = g_data.takeoff_time;
        pthread_mutex_unlock(&g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Czekam %d sekund na odlot.\n" ANSI_COLOR_RESET, flight_no, takeoff_time);

        pthread_mutex_lock(&g_data_mutex);
        int new_counter = g_data.check_counter;
        pthread_mutex_unlock(&g_data_mutex);

        if (new_counter == 1) {
            pthread_mutex_lock(&g_data_mutex);
            g_data.check_counter = 0;
            pthread_mutex_unlock(&g_data_mutex);
        } else {
            sleep(takeoff_time); // Opóźnienie przed odlotem
        }

        /* Start lotu – zamykamy boarding */
        pthread_mutex_lock(&g_data_mutex);
        g_data.plane_in_flight = 1;  // lot jest już "zamknięty"
        pthread_mutex_unlock(&g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Odlatuję z %d pasażerami. (Bagaz: %d/%d)\n" ANSI_COLOR_RESET,
               flight_no, ppl, g_data.plane_sum_of_luggage, g_data.plane_luggage_capacity);

        /* Symulacja lotu i lądowania */
//        sleep(2);

        pthread_mutex_lock(&g_data_mutex);
        g_data.finished_passengers += ppl;
        int fin_now = g_data.finished_passengers;
        int tot = g_data.total_passengers;
        g_data.people_in_plane = 0;
        int rejected = g_data.passengers_rejected;
        int mad = g_data.passengers_mad;
        pthread_mutex_unlock(&g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Wylądowaliśmy. %d pasażerów doleciało. (finished=%d/%d)\n" ANSI_COLOR_RESET,
               flight_no, ppl, fin_now, tot);

        FILE *fp = fopen("raport_samoloty.txt", "a");
        if (fp == NULL) {
            perror("fopen(raport_samoloty.txt) error");
        } else {
            fprintf(fp, "Lot %d zakończony: przewieziono %d pasażerów, odrzuceni: %d, zli: %d, ogólna liczba zakończonych: %d/%d.\n",
                    flight_no, ppl, rejected, mad, fin_now, tot);
            fclose(fp);
        }

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Wróciłem.\n" ANSI_COLOR_RESET, flight_no);

        // Reset stanu dla kolejnego lotu:
        pthread_mutex_lock(&g_data_mutex);
        g_data.plane_in_flight = 0;
        g_data.people_in_plane = 0;
        pthread_mutex_unlock(&g_data_mutex);
    } // koniec głównej pętli lotów

plane_end:
    printf(ANSI_COLOR_BLUE "[PLANE] Kończę wątek samolotu - nie będzie więcej lotów.\n" ANSI_COLOR_RESET);
    pthread_exit(NULL);
}