#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "shared.h"
#include "dispatcher.h"

void *dispatcher_thread(void *arg)
{
    (void)arg;
    printf(ANSI_COLOR_CYAN"[DISPATCHER] Start.\n" ANSI_COLOR_RESET);

    while (1) {
//        sleep(3);

        pthread_mutex_lock(&g_data_mutex);
        int still_active = g_data.is_simulation_active;
        int gen_count = g_data.generated_count;
        int total = g_data.total_passengers;
        int finished = g_data.finished_passengers;
        int ppl_in_plane = g_data.people_in_plane;
    	int stop_gen = g_data.stop_generating;
    	int stairs = g_data.stairs_occupancy;
        int plane_in_flight = g_data.plane_in_flight;
        int check_counter = g_data.check_counter;
        pthread_mutex_unlock(&g_data_mutex);

    	pthread_mutex_lock(&hall_mutex);
    	int hall_empty = ((vip_head == NULL) && (normal_head == NULL));
    	pthread_mutex_unlock(&hall_mutex);

//        printf(ANSI_COLOR_CYAN"[DISPATCHER] Raport: gen=%d/%d, finished=%d, inPlane=%d\n" ANSI_COLOR_RESET,
//               gen_count, total, finished, ppl_in_plane);

//        print_hall_queues();

        if (check_counter == 1) {
            // Po 1 sek -> przyspiesz najblizszy start
            sleep(check_counter);
            printf(ANSI_COLOR_CYAN"[DISPATCHER] Wysyłam sygnał SIGUSR1, by przyspieszyć lot.\n" ANSI_COLOR_RESET);
        	raise(SIGUSR1);
        }

    	if (check_counter == 2) {
    		// Po 2 sek -> zamykam odprawę biletowo-bagażową
    		sleep(check_counter);
    		printf(ANSI_COLOR_CYAN"[DISPATCHER] Wysyłam sygnał SIGUSR2 - zamykam odprawę biletowo-bagażową!\n" ANSI_COLOR_RESET);
    		raise(SIGUSR2);
        }

        // Jeśli wszyscy pasażerowie skończyli – koniec
    	if ((gen_count >= total || stop_gen) && (finished >= gen_count) && hall_empty && stairs == 0) {
    		pthread_mutex_lock(&g_data_mutex);
    		g_data.is_simulation_active = 0;
    		still_active = g_data.is_simulation_active;
    		pthread_mutex_unlock(&g_data_mutex);
    		printf(ANSI_COLOR_CYAN"[DISPATCHER] Warunek końca: generated=%d, finished=%d, stairs=%d, hall_empty=%d.\n" ANSI_COLOR_RESET,
				   gen_count, finished, stairs, hall_empty);
    		pthread_mutex_lock(&hall_mutex);
    		while (vip_head != NULL || normal_head != NULL) {
    			hall_node *node = dequeue_hall();
    			if (node) {
    				// Wybudź pasażera
    				sem_post(node->board_sem);

    				// Zaktualizuj finished_passengers
    				pthread_mutex_lock(&g_data_mutex);
    				g_data.finished_passengers++;
    				pthread_mutex_unlock(&g_data_mutex);

    				// Zamknij semafor i usuń węzeł
    				sem_close(node->board_sem);
    				safe_sem_unlink(node->sem_name);
    				free(node);
    			}
    		}
    		pthread_mutex_unlock(&hall_mutex);
            break;
		}

    	if (stop_gen == 1 && (finished + ppl_in_plane + stairs == gen_count) && hall_empty && !plane_in_flight) {
    		// Wszyscy, którzy zostali faktycznie wygenerowani (gen_count) -> skończyli
    		pthread_mutex_lock(&g_data_mutex);
    		g_data.is_simulation_active = 0;
    		still_active = g_data.is_simulation_active;
    		pthread_mutex_unlock(&g_data_mutex);
    		printf(ANSI_COLOR_CYAN"[DISPATCHER] Sygnał2 i wszyscy (%d) już przewiezieni/obsłużeni -> koniec.\n" ANSI_COLOR_RESET, finished);
    		break;
    	}

        if (!still_active) {
            printf(ANSI_COLOR_CYAN"[DISPATCHER] is_simulation_active=0 -> kończę.\n" ANSI_COLOR_RESET);
            break;
        }

        //sleep(1);
    }

    printf(ANSI_COLOR_CYAN"[DISPATCHER] Kończę wątek.\n" ANSI_COLOR_RESET);
    pthread_exit(NULL);
}