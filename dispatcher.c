#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "shared.h"
#include "dispatcher.h"
#include <time.h>

//struct timespec start, now;
//clock_gettime(CLOCK_REALTIME, &start);

void *dispatcher_thread(void *arg)
{
    (void)arg;
    printf(ANSI_COLOR_CYAN"[DISPATCHER] Start.\n" ANSI_COLOR_RESET);
//    int check_counter = 0;

    while (1) {
//        sleep(3);
//        check_counter += 3;

        pthread_mutex_lock(&g_data.g_data_mutex);
        int still_active = g_data.is_simulation_active;
        int gen_count = g_data.generated_count;
        int total = g_data.total_passengers;
        int finished = g_data.finished_passengers;
        int ppl_in_plane = g_data.people_in_plane;
    	int stop_gen = g_data.stop_generating;
    	int hall_empty = ((vip_head == NULL) && (normal_head == NULL));
    	int stairs    = g_data.stairs_occupancy;
        //int rejected_passengers = g_data.passengers_rejected;
        pthread_mutex_unlock(&g_data.g_data_mutex);

//        printf(ANSI_COLOR_CYAN"[DISPATCHER] Raport: gen=%d/%d, finished=%d, inPlane=%d\n" ANSI_COLOR_RESET,
//               gen_count, total, finished, ppl_in_plane);
//
//        print_hall_queues();

//        if (check_counter == 17) {//nie bedzie takiej wartosci wiec sie nie wykona
//            // Po 18 sek -> przyspiesz start
//            printf(ANSI_COLOR_CYAN"[DISPATCHER] Wysyłam sygnał SIGUSR1, by przyspieszyć lot.\n" ANSI_COLOR_RESET);
//
//        }
//
//    	if (check_counter == 20) {//nie bedzie takiej wartosci wiec sie nie wykona
//    		printf(ANSI_COLOR_CYAN"[DISPATCHER] Wysyłam sygnał SIGUSR2 - zamykam odprawę biletowo-bagażową!\n" ANSI_COLOR_RESET);
//    		raise(SIGUSR1);
//        }

        // Jeśli wszyscy pasażerowie skończyli – koniec
    	if ((gen_count >= total || stop_gen)
			&& (finished >= gen_count)
			&& hall_empty
			&& stairs == 0
		) {
    		pthread_mutex_lock(&g_data.g_data_mutex);
    		g_data.is_simulation_active = 0;
    		pthread_mutex_unlock(&g_data.g_data_mutex);
    		printf("[DISPATCHER] Warunek końca: generated=%d, finished=%d, stairs=%d, hall_empty=%d.\n",
				   gen_count, finished, stairs, hall_empty);
    		break;
		}

    	if (stop_gen == 1 && (finished + ppl_in_plane + stairs >= gen_count) && hall_empty) {
    		// Wszyscy, którzy zostali faktycznie wygenerowani (gen_count) -> skończyli
    		pthread_mutex_lock(&g_data.g_data_mutex);
    		g_data.is_simulation_active = 0;
    		still_active = g_data.is_simulation_active;
    		pthread_mutex_unlock(&g_data.g_data_mutex);
    		printf(ANSI_COLOR_CYAN"[DISPATCHER] Sygnał2 i wszyscy (%d) już przewiezieni/obsłużeni -> koniec.\n" ANSI_COLOR_RESET, finished);
    		break;
    	}

        if (!still_active) {
            printf(ANSI_COLOR_CYAN"[DISPATCHER] is_simulation_active=0 -> kończę.\n" ANSI_COLOR_RESET);
            break;
        }
    }

    printf(ANSI_COLOR_CYAN"[DISPATCHER] Kończę wątek.\n" ANSI_COLOR_RESET);
    pthread_exit(NULL);
}