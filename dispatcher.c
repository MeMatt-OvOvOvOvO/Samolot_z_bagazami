#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "shared.h"
#include "dispatcher.h"

void *dispatcher_thread(void *arg)
{
    (void)arg;
    printf("[DISPATCHER] Start.\n");
    int check_counter = 0;

    while (1) {
        sleep(3);
        check_counter += 3;

        pthread_mutex_lock(&g_data.g_data_mutex);
        int still_active = g_data.is_simulation_active;
        int gen_count = g_data.generated_count;
        int total = g_data.total_passengers;
        int finished = g_data.finished_passengers;
        int ppl_in_plane = g_data.people_in_plane;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[DISPATCHER] Raport: gen=%d/%d, finished=%d, inPlane=%d\n",
               gen_count, total, finished, ppl_in_plane);

        if (check_counter == 15) {
            // Po 15 sek -> przyspiesz start
            printf("[DISPATCHER] Wysyłam sygnał SIGUSR1, by przyspieszyć lot.\n");
            raise(SIGUSR1);
        }

        // Jeśli wszyscy pasażerowie skończyli – koniec
        if (finished >= total && ppl_in_plane == 0) {
    		// ALE sprawdźmy, czy people_in_plane>0
    		pthread_mutex_lock(&g_data.g_data_mutex);
    		int in_plane = g_data.people_in_plane;
    		pthread_mutex_unlock(&g_data.g_data_mutex);

    		if (in_plane == 0) {
        		// dopiero wtedy ogłaszamy koniec
        		pthread_mutex_lock(&g_data.g_data_mutex);
        		g_data.is_simulation_active = 0;
        		pthread_mutex_unlock(&g_data.g_data_mutex);
        		printf("[DISPATCHER] Wszyscy pasażerowie (%d) zakończyli.\n", finished);
        		break;
    		}
    		// jeśli in_plane>0, to czekamy dalej,
    		// bo samolot jeszcze leci i wyląduje za chwilę
		}

        if (!still_active) {
            printf("[DISPATCHER] is_simulation_active=0 -> kończę.\n");
            break;
        }
    }

    printf("[DISPATCHER] Kończę wątek.\n");
    pthread_exit(NULL);
}