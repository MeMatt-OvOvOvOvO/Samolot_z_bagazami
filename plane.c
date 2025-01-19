#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "shared.h"
#include "plane.h"

void *plane_thread(void *arg)
{
    (void)arg;
    printf(ANSI_COLOR_BLUE "[PLANE] Start - wielokrotne loty.\n" ANSI_COLOR_RESET);

    int flight_no = 0;

    while (1) {
        // Sprawdzenie, czy symulacja nadal aktywna
        pthread_mutex_lock(&g_data.g_data_mutex);
        int finished = g_data.finished_passengers;
        int total = g_data.total_passengers;
        int active = g_data.is_simulation_active;
        int plane_capacity = g_data.plane_capacity;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (!active || finished >= total) {
            printf(ANSI_COLOR_BLUE "[PLANE] Nie ma potrzeby kolejnego lotu (finished=%d/%d, active=%d).\n" ANSI_COLOR_RESET,
                   finished, total, active);
            break;
        }

        // Przygotowanie nowego lotu:
        flight_no++;
        int random_factor = 7 + (rand() % 4); // losowo z przedziału 7..10
        int plane_luggage_capacity = plane_capacity * random_factor;
        int plane_sum_of_luggage = 0;
        int attempt_to_fit_again = 0;

        // Ustawienie parametrów nowego lotu
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_sum_of_luggage = plane_sum_of_luggage;
        g_data.plane_luggage_capacity = plane_luggage_capacity;
        g_data.people_in_plane = 0;
        g_data.plane_in_flight = 0;    // lot otwarty – boarding dostępny
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Nowy lot – limit bagażu = %d.\n" ANSI_COLOR_RESET,
               flight_no, plane_luggage_capacity);

        /******** BOARDING ********/
        // Przechodzimy w pętlę pobierania pasażerów z kolejki (holu)
        while (1) {
            // Sprawdź stan boardingu:
            pthread_mutex_lock(&g_data.g_data_mutex);
            int plane_now = g_data.people_in_plane;
            int capacity = g_data.plane_capacity;
            int active2 = g_data.is_simulation_active;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            if (!active2) {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Symulacja nieaktywna.\n" ANSI_COLOR_RESET, flight_no);
                goto plane_end;
            }
            if (plane_now >= capacity) {
                // Samolot pełny – kończymy boarding
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Samolot pełny (%d/%d). Boardowanie zakończone.\n" ANSI_COLOR_RESET,
                       flight_no, plane_now, capacity);
                break;
            }

            // Pobieramy pasażera z kolejki holu:
            hall_node *hn = dequeue_hall();
            if (!hn) {
                // Hol jest pusty – wątek samolotu czeka na pojawienie się nowego pasażera
                pthread_mutex_lock(&hall_mutex);
                // Jeśli hol faktycznie pusty:
                int hallEmpty = ((vip_head == NULL) && (normal_head == NULL));
                pthread_mutex_unlock(&hall_mutex);

                pthread_mutex_lock(&g_data.g_data_mutex);
                int stop_gen = g_data.stop_generating;
                int ppl = g_data.people_in_plane;
                pthread_mutex_unlock(&g_data.g_data_mutex);

                // Jeśli hol pusty oraz już są pasażerowie w samolocie
                // (oraz, opcjonalnie, nie spodziewamy się już nowych, czyli stop_gen==1),
                // wychodzimy z pętli boardingu i przechodzimy do startu.
                if (hallEmpty && ppl > 0 && stop_gen) {
                    printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Hol pusty, a boardowanie ma już %d pasażerów. Przechodzę do startu.\n" ANSI_COLOR_RESET,
                           flight_no, ppl);
                    break;
                }

                // Jeśli hol pusty, ale boardowanie jeszcze trwa,
                // oczekujemy, aż pojawi się nowy pasażer w holu.
                pthread_mutex_lock(&hall_mutex);
                pthread_cond_wait(&hall_not_empty_cond, &hall_mutex);
                pthread_mutex_unlock(&hall_mutex);
                continue;
            }

            // Mamy pasażera z holu:
            int pid = hn->passenger_id;
            int vip = hn->is_vip;
            int bw  = hn->bag_weight;

            // Odczytaj aktualną sumę bagażu – przyjmujemy, że jest aktualizowana w g_data
            pthread_mutex_lock(&g_data.g_data_mutex);
            plane_sum_of_luggage = g_data.plane_sum_of_luggage;
            int plane_limit = g_data.plane_luggage_capacity;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            if (plane_sum_of_luggage + bw <= plane_limit) {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Zapraszam pasażera %d (VIP=%d, bag=%d).\n" ANSI_COLOR_RESET,
                       flight_no, pid, vip, bw);

                // Wysyłamy sygnał do pasażera, by rozpoczął boardowanie
                sem_post(hn->board_sem);

                // Po wywołaniu sem_post, samolot zwalnia uchwyt i pamięć dla tego węzła.
                if (sem_close(hn->board_sem) != 0) {
                    perror("sem_close(hn->board_sem)");
                }
                safe_sem_unlink(hn->sem_name);
                free(hn);
                attempt_to_fit_again = 0;
            }
            else {
                printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Pasażer %d (bag=%d) NIE mieści się (sum=%d, limit=%d).\n" ANSI_COLOR_RESET,
                       flight_no, pid, bw, plane_sum_of_luggage, plane_limit);
                // Odkładamy pasażera z powrotem do holu
                enqueue_hall(pid, vip, bw);
                free(hn);
                if (attempt_to_fit_again == 0) {
                    printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Spróbuję jeszcze raz pobrać innego pasażera.\n" ANSI_COLOR_RESET, flight_no);
                    attempt_to_fit_again = 1;
                }
                else {
                    printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Drugi pasażer się nie mieści – lot opóźniony, odlatuję!\n" ANSI_COLOR_RESET, flight_no);
                    break;
                }
            }
        } // koniec pętli boardingu

        /******** Po zakończeniu boardingu: ********/
        pthread_mutex_lock(&g_data.g_data_mutex);
        int ppl = g_data.people_in_plane;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (ppl == 0) {
            printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) 0 pasażerów -> rezygnuję z lotu.\n" ANSI_COLOR_RESET, flight_no);
            goto plane_end;
        }
        else {
            printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Boardowanie zakończone: %d pasażerów wsiadło.\n" ANSI_COLOR_RESET, flight_no, ppl);
        }

        // Start lotu – zamykamy drzwi
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_in_flight = 1;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Odlatuję z %d pasażerami. (Bagaz: %d/%d)\n" ANSI_COLOR_RESET,
               flight_no, ppl, plane_sum_of_luggage, g_data.plane_luggage_capacity);

        /* Tutaj symulujemy lot (możesz zaimplementować boardowanie opóźnione itp.) */

        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.finished_passengers += ppl;
        int fin_now = g_data.finished_passengers;
        int tot = g_data.total_passengers;
        g_data.people_in_plane = 0;  // opróżniamy samolot
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Wylądowaliśmy. %d pasażerów doleciało. (finished=%d/%d)\n" ANSI_COLOR_RESET,
               flight_no, ppl, fin_now, tot);

        printf(ANSI_COLOR_BLUE "[PLANE] (Lot %d) Wróciłem.\n" ANSI_COLOR_RESET, flight_no);
    } // koniec głównej pętli lotów

plane_end:
    printf(ANSI_COLOR_BLUE "[PLANE] Kończę wątek samolotu - nie będzie więcej lotów.\n" ANSI_COLOR_RESET);
    pthread_exit(NULL);
}