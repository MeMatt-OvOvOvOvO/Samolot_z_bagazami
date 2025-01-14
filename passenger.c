#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include "shared.h"
#include "passenger.h"

int enter_security_check(int gender, int is_vip, int passenger_id);

void enter_stairs_and_plane(int id);

/*******************************************************
 * GENERATOR PASAŻERÓW
 *******************************************************/
void *passenger_generator_thread(void *arg)
{
    (void)arg;
    printf("[GENERATOR] Startuje. Bede tworzyl pasazerow.\n");

    while (1) {
        pthread_mutex_lock(&g_data.g_data_mutex);
        int current_count = g_data.generated_count;
        int max_count = g_data.total_passengers;
        int active = g_data.is_simulation_active;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (!active) {
            printf("[GENERATOR] Symulacja nieaktywna - kończę.\n");
            break;
        }
        if (current_count >= max_count) {
            printf("[GENERATOR] Wygenerowano wszystkich %d pasażerów.\n", max_count);
            break;
        }

        sleep((rand() % 2) + 1);

        pthread_t th_passenger;
        int *passenger_id = malloc(sizeof(int));
        if (!passenger_id) {
            perror("malloc(passenger_id)");
            continue;
        }
        *passenger_id = current_count + 1;

        if (pthread_create(&th_passenger, NULL, passenger_thread, passenger_id) != 0) {
            perror("pthread_create(passenger)");
            free(passenger_id);
            continue;
        }
        if (pthread_detach(th_passenger) != 0) {
            perror("pthread_detach(passenger)");
        }

        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.generated_count++;
        pthread_mutex_unlock(&g_data.g_data_mutex);
    }

    printf("[GENERATOR] Kończę.\n");
    pthread_exit(NULL);
}

/*******************************************************
 * Pojedynczy pasażer
 *******************************************************/
void *passenger_thread(void *arg)
{
    int my_id = *((int *)arg);
    free(arg);

    int bag_weight = rand() % 10 + 1; // 1-10 kg
    int is_vip = (rand() % 5 == 0) ? 1 : 0; // co piaty VIP
    int gender = (rand() % 2); // 0 = mezczyzna, 1 = kobieta

    printf("[PASSENGER %d] Jestem watkiem pasazera (bagaż=%d, VIP=%d, gender=%d)\n",
           my_id, bag_weight, is_vip, gender);

    /* Odprawa bagażowa */
    if (sem_wait(g_data.baggage_check_sem) != 0) {
        perror("sem_wait(baggage_check_sem)");
        goto finish_passenger;
    }
    printf("[PASSENGER %d] Odprawa bagażowa...\n", my_id);
    sleep(1);

    if (bag_weight > g_data.baggage_limit) {
        printf("[PASSENGER %d] Odrzucony (bagaż=%d > %d)\n", my_id, bag_weight, g_data.baggage_limit);
        sem_post(g_data.baggage_check_sem);
        goto finish_passenger;
    }
    printf("[PASSENGER %d] Odprawa OK.\n", my_id);
    sem_post(g_data.baggage_check_sem);

    /* Kontrola bezpieczeństwa */
    if (!enter_security_check(gender, is_vip, my_id)) {
        // 0 => pasażer odrzucony, kończymy
        goto finish_passenger;
    }

    /* Schody -> Samolot */
    enter_stairs_and_plane(my_id);

    printf("[PASSENGER %d] W samolocie, czeka na odlot.\n", my_id);
    sleep(1);

finish_passenger:
    pthread_mutex_lock(&g_data.g_data_mutex);
    g_data.finished_passengers++;
    pthread_mutex_unlock(&g_data.g_data_mutex);

    printf("[PASSENGER %d] Kończy.\n", my_id);
    pthread_exit(NULL);
}

/* =====================================================
   enter_security_check(gender, vip)
   - Utrzymujemy 'station_occupancy[i]'
   - Jeśli occupancy == 0 -> station_gender[i] = -1
   ===================================================== */
int enter_security_check(int gender, int is_vip, int passenger_id)
{
    int wait_count = 0;

    while (1) {
        int found_station = -1;

        pthread_mutex_lock(&g_data.station_mutex);

        if (is_vip) {
            // spróbuj wcisnąć się w dowolne stanowisko
            for (int i = 0; i < SECURITY_STATIONS; i++) {
                if (g_data.station_occupancy[i] < 2) {
                    if (g_data.station_gender[i] == -1) {
                        g_data.station_gender[i] = gender;
                        found_station = i;
                        break;
                    }
                    else if (g_data.station_gender[i] == gender) {
                        found_station = i;
                        break;
                    }
                }
            }
        }

        if (found_station < 0) {
            // normalna ścieżka
            for (int i = 0; i < SECURITY_STATIONS; i++) {
                if (g_data.station_gender[i] == -1) {
                    g_data.station_gender[i] = gender;
                    found_station = i;
                    break;
                }
                else if (g_data.station_gender[i] == gender &&
                         g_data.station_occupancy[i] < 2) {
                    found_station = i;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&g_data.station_mutex);

        if (found_station >= 0) {
            // stacja znaleziona
            if (sem_wait(g_data.security_sem[found_station]) != 0) {
                perror("sem_wait(security_sem)");
                continue;
            }

            pthread_mutex_lock(&g_data.station_mutex);
            g_data.station_occupancy[found_station]++;
            //int occ_now = g_data.station_occupancy[found_station];
            pthread_mutex_unlock(&g_data.station_mutex);

            sleep(1);

            int dangerous = (rand() % 100 < 5);
            if (dangerous) {
                if (sem_post(g_data.security_sem[found_station]) != 0) {
                    perror("sem_post(security_sem[found_station])");
                }

                pthread_mutex_lock(&g_data.station_mutex);
                g_data.station_occupancy[found_station]--;
                int occ_after = g_data.station_occupancy[found_station];
                if (occ_after == 0) {
                    g_data.station_gender[found_station] = -1;
                    printf("[SECURITY] Stanowisko %d PUSTE.\n", found_station);
                }
                pthread_mutex_unlock(&g_data.station_mutex);

                printf("[DETAILED] Pasażer %d - WYKRYTO niebezpieczny przedmiot, odrzucony!\n", passenger_id);
                return 0;
            }
            else {
                printf("[DETAILED] Pasażer %d - Kontrola OK.\n", passenger_id);

                if (sem_post(g_data.security_sem[found_station]) != 0) {
                    perror("sem_post(security_sem[found_station])");
                }

                pthread_mutex_lock(&g_data.station_mutex);
                g_data.station_occupancy[found_station]--;
                int occ_after = g_data.station_occupancy[found_station];
                if (occ_after == 0) {
                    g_data.station_gender[found_station] = -1;
                    printf("[SECURITY] Stanowisko %d PUSTE.\n", found_station);
                }
                pthread_mutex_unlock(&g_data.station_mutex);

                return 1;
            }
        }
        else {
            // NIE znaleźliśmy stacji (wszystkie zajęte inną płcią lub brak miejsca)
            wait_count++;
            if (wait_count > 3) {
                printf("[SECURITY] Pasażer %d jest zły (czekał dłużej niż 3 razy) i rezygnuje!\n", passenger_id);
                return 0;
            }

            sleep(1);
        }
    }
}
/*******************************************************
 * enter_stairs_and_plane(id)
 * -> czekamy na schody, schodzimy,
 * -> sprawdzamy, czy samolot nie jest w locie (plane_in_flight)
 *    jeśli jest, czekamy na nowy
 * -> increment people_in_plane
 *******************************************************/
void enter_stairs_and_plane(int id)
{
    while (1) {
        pthread_mutex_lock(&g_data.g_data_mutex);
        int in_flight = g_data.plane_in_flight;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (in_flight == 1) {
            printf("[HALL] Pasażer %d czeka, bo samolot jest w locie...\n", id);
            sleep(1);
        } else {
            // samolot dostępny (in_flight=0)
            break;
        }
    }

    // Teraz można wchodzić na schody
    if (sem_wait(g_data.stairs_sem) != 0) {
        perror("sem_wait(stairs_sem)");
        return;
    }
    printf("[STAIRS] Pasażer %d WCHODZI na schody\n", id);
    sleep(2);
    printf("[STAIRS] Pasażer %d ZSZEDŁ ze schodów\n", id);
    if (sem_post(g_data.stairs_sem) != 0) {
        perror("sem_post(stairs_sem)");
    }

    // Zwiększamy licznik w samolocie
    pthread_mutex_lock(&g_data.g_data_mutex);
    g_data.people_in_plane++;
    int now_in_plane = g_data.people_in_plane;
    pthread_mutex_unlock(&g_data.g_data_mutex);

    printf("[PLANE] Pasażer %d zajął miejsce (inPlane=%d)\n", id, now_in_plane);
}