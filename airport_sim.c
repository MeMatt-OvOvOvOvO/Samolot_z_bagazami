/*******************************************************
 * Program przyjmuje jeden parametr:
 *   ./airport_sim <liczba_pasazerow_do_wygenerowania> <limit_bagazu>
 * Kompilacja:
 *   gcc -Wall -o airport_sim airport_sim.c -pthread
 *******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#define SECURITY_STATIONS 3

struct {
    int total_passengers;
    int generated_count;
    int is_simulation_active;
    int baggage_limit;
    sem_t *baggage_check_sem;

    /* TABLICA WSKAŹNIKÓW do semaforów nazwanych (3 stanowiska).
     * Każdy semafor ma początkową wartość = 2. */
    sem_t *security_sem[SECURITY_STATIONS];

    /* (0=male, 1=female, -1=empty) */
    int station_gender[SECURITY_STATIONS];

    int station_occupancy[SECURITY_STATIONS];

    pthread_mutex_t station_mutex;
    pthread_mutex_t g_data_mutex;
} g_data;

/* Watek Dyspozytora (Wiezy Kontroli) */
void *dispatcher_thread(void *arg);

/* Watek Samolotu (Kapitan) */
void *plane_thread(void *arg);

/* Watek generatora pasazerow - tworzy watki pasazerow */
void *passenger_generator_thread(void *arg);

/* Kazdy Pasazer tez bedzie watkiem */
void *passenger_thread(void *arg);

void enter_security_check(int gender);

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Uzycie: %s <liczba_pasazerow> <limit_bagazu>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Parsujemy liczbe pasazerow z argumentu. */
    g_data.total_passengers = atoi(argv[1]);
    if (g_data.total_passengers <= 0) {
        fprintf(stderr, "Niepoprawna liczba pasazerow: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    g_data.baggage_limit = atoi(argv[2]);
    if (g_data.baggage_limit <= 0) {
        fprintf(stderr, "Niepoprawny limit bagazu: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    g_data.generated_count = 0;
    g_data.is_simulation_active = 1;

    /* Inicjujemy losowosc */
    srand(time(NULL));

    sem_unlink("/baggage_check_sem"); // gdyby poprzedni semafor nie byl usuniety
    g_data.baggage_check_sem = sem_open("/baggage_check_sem", O_CREAT, 0600, 1);
    if (g_data.baggage_check_sem == SEM_FAILED) {
        perror("sem_open(baggage_check_sem) error");
        return EXIT_FAILURE;
    }

    /* 2) Semafory nazwane do 3 stanowisk bezpieczeństwa */
    for (int i = 0; i < SECURITY_STATIONS; i++) {
        char name[32];
        sprintf(name, "/security_sem_%d", i);

        sem_unlink(name); // na wypadek, gdyby istniał
        g_data.security_sem[i] = sem_open(name, O_CREAT, 0600, 2);
        if (g_data.security_sem[i] == SEM_FAILED) {
            perror("sem_open(security_sem[i])");
            return EXIT_FAILURE;
        }

        g_data.station_gender[i]    = -1; // puste
        g_data.station_occupancy[i] = 0;  // brak osób
    }

    /* 3) Inicjujemy muteksy */
    if (pthread_mutex_init(&g_data.station_mutex, NULL) != 0) {
        perror("pthread_mutex_init(station_mutex)");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&g_data.g_data_mutex, NULL) != 0) {
        perror("pthread_mutex_init(g_data_mutex)");
        exit(EXIT_FAILURE);
    }

    printf("[MAIN] Start symulacji: %d pasazerow, limit bagazu = %d kg.\n",
           g_data.total_passengers, g_data.baggage_limit);

    /* Tworzymy watki: dispatcher, plane, passenger_generator */
    pthread_t th_dispatcher, th_plane, th_generator;

    if (pthread_create(&th_dispatcher, NULL, dispatcher_thread, NULL) != 0) {
        perror("pthread_create(dispatcher) error");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&th_plane, NULL, plane_thread, NULL) != 0) {
        perror("pthread_create(plane) error");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&th_generator, NULL, passenger_generator_thread, NULL) != 0) {
        perror("pthread_create(generator) error");
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

    /* Sprzątanie:
     * - zamykamy semafor nazwany bagażowy
     * - unlink
     * - niszczymy semafory security
     * - niszczymy muteksy
     */
    // Odprawa biletowo-bagażowa
    if (sem_close(g_data.baggage_check_sem) != 0) {
        perror("sem_close(baggage_check_sem)");
    }
    if (sem_unlink("/baggage_check_sem") != 0) {
        perror("sem_unlink(baggage_check_sem)");
    }

    // Stanowiska bezpieczeństwa
    for (int i = 0; i < SECURITY_STATIONS; i++) {
        if (sem_close(g_data.security_sem[i]) != 0) {
            perror("sem_close(security_sem[i])");
        }
        char name[32];
        sprintf(name, "/security_sem_%d", i);
        if (sem_unlink(name) != 0) {
            perror("sem_unlink(security_sem[i])");
        }
    }

    pthread_mutex_destroy(&g_data.station_mutex);
    pthread_mutex_destroy(&g_data.g_data_mutex);

    printf("[MAIN] Symulacja zakończona.\n");
    return EXIT_SUCCESS;
}

void *dispatcher_thread(void *arg)
{
    (void) arg;

    printf("[DISPATCHER] Startuje.\n");

    while (1) {
        sleep(3);

        pthread_mutex_lock(&g_data.g_data_mutex);
        int still_active = g_data.is_simulation_active;
        int gen_count = g_data.generated_count;
        int total = g_data.total_passengers;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        printf("[DISPATCHER] Raport: wygenerowano %d / %d pasazerow.\n",
               gen_count, total);

        if (!still_active) {
            printf("[DISPATCHER] Koncze prace (is_simulation_active=0).\n");
            break;
        }
    }

    pthread_exit(NULL);
}

void *plane_thread(void *arg)
{
    (void) arg;

    printf("[PLANE] Startuje.\n");

    while (1) {
        sleep(2);

        pthread_mutex_lock(&g_data.g_data_mutex);
        int still_active = g_data.is_simulation_active;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (!still_active) {
            printf("[PLANE] Koncze prace (is_simulation_active=0).\n");
            break;
        }

    }

    pthread_exit(NULL);
}

void *passenger_generator_thread(void *arg)
{
    (void) arg;

    printf("[GENERATOR] Startuje. Bede tworzyl pasazerow.\n");

    while (1) {
        pthread_mutex_lock(&g_data.g_data_mutex);
        int current_count = g_data.generated_count;
        int max_count = g_data.total_passengers;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (current_count >= max_count) {
            printf("[GENERATOR] Osiagnieto limit %d pasazerow.\n", max_count);

            pthread_mutex_lock(&g_data.g_data_mutex);
            g_data.is_simulation_active = 0;
            pthread_mutex_unlock(&g_data.g_data_mutex);

            break;
        }

        /* Losowo "odczekaj" przed stworzeniem kolejnego pasazera */
        int wait_time = rand() % 2 + 1;
        sleep(wait_time);

        /* Tworzymy nowy watek pasazera */
        pthread_t th_passenger;
        int *passenger_id = malloc(sizeof(int));
        if (!passenger_id) {
            perror("malloc() error");
            continue;
        }

        *passenger_id = current_count + 1;

        if (pthread_create(&th_passenger, NULL, passenger_thread, passenger_id) != 0) {
            perror("pthread_create(passenger) error");
            free(passenger_id);
            continue;
        }

        if (pthread_detach(th_passenger) != 0) {
            perror("pthread_detach(passenger) error");
        }

        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.generated_count++;
        pthread_mutex_unlock(&g_data.g_data_mutex);
    }

    printf("[GENERATOR] Koncze prace.\n");
    pthread_exit(NULL);
}

void *passenger_thread(void *arg)
{
    int my_id = *((int *)arg);
    free(arg); // zwalniamy pamiec zaalokowana w generatorze

    int bag_weight = rand() % 10 + 1; // 1-10 kg
    int is_vip = (rand() % 5 == 0) ? 1 : 0; // co piaty VIP
    int gender = (rand() % 2); // 0 = mezczyzna, 1 = kobieta

    printf("[PASSENGER %d] Jestem watkiem pasazera (bagaz=%d kg, VIP=%d, gender=%d)\n",
           my_id, bag_weight, is_vip, gender);

    printf("[PASSENGER %d] Czeka na dostep do stanowiska odprawy...\n", my_id);

    if (sem_wait(g_data.baggage_check_sem) != 0) {
        perror("sem_wait(baggage_check_sem) error");
        pthread_exit(NULL);
    }

    printf("[PASSENGER %d] Jestem w odprawie bagazowej...\n", my_id);
    sleep(1);

    int limit = g_data.baggage_limit;
    if (bag_weight > limit) {
        printf("[PASSENGER %d] ODRZUCONY - bagaż %d > limit %d.\n",
               my_id, bag_weight, limit);

        sem_post(g_data.baggage_check_sem);
        pthread_exit(NULL);
    }

    printf("[PASSENGER %d] Odprawa OK.\n", my_id);

    sem_post(g_data.baggage_check_sem);

    // Kontrola bezpieczeństwa
    enter_security_check(gender);

    // po przejściu kontroli bezpieczeństwa...
    printf("[PASSENGER %d] Przeszedł kontrolę bezpieczeństwa, czeka na samolot...\n", my_id);

    // w przyszłości: czeka w holu, idzie na schody, itp.
    sleep(1);

    printf("[PASSENGER %d] Kończy (dotarł do kolejnego etapu).\n", my_id);
    pthread_exit(NULL);
}

/* =====================================================
   enter_security_check(gender)
   - Utrzymujemy 'station_occupancy[i]'
   - Jeśli occupancy == 0 -> station_gender[i] = -1
   ===================================================== */
void enter_security_check(int gender)
{
    while (1) {
        int found_station = -1;

        /* Szukamy stanowiska, do którego możemy wejść */
        pthread_mutex_lock(&g_data.station_mutex);

        for (int i = 0; i < SECURITY_STATIONS; i++) {
            int st_gender    = g_data.station_gender[i];
            int st_occupancy = g_data.station_occupancy[i];

            if (st_gender == -1) {
                // puste stanowisko -> rezerwujemy dla danej płci
                g_data.station_gender[i] = gender;
                found_station = i;
                break;
            } else if (st_gender == gender && st_occupancy < 2) {
                // ta sama płeć i jest jeszcze miejsce (max 2)
                found_station = i;
                break;
            }
        }

        pthread_mutex_unlock(&g_data.station_mutex);

        if (found_station >= 0) {
            // Mamy stację, próbujemy semafora
            if (sem_wait(g_data.security_sem[found_station]) != 0) {
                perror("sem_wait(security_sem)");
                // w razie błędu ponawiamy
                continue;
            }

            // Udało się przejąć semafor -> wchodzimy
            pthread_mutex_lock(&g_data.station_mutex);
            g_data.station_occupancy[found_station]++;  // pasażer doszedł
            int occupancy_now = g_data.station_occupancy[found_station];
            pthread_mutex_unlock(&g_data.station_mutex);

            printf("[SECURITY] Pasażer(gender=%d) WCHODZI do st.%d (occupancy=%d)\n",
                   gender, found_station, occupancy_now);

            // Symulacja kontroli
            sleep(1);

            // Wychodzimy
            if (sem_post(g_data.security_sem[found_station]) != 0) {
                perror("sem_post(security_sem)");
            }

            // Zmniejszamy liczbę osób
            pthread_mutex_lock(&g_data.station_mutex);
            g_data.station_occupancy[found_station]--;
            int occ_after = g_data.station_occupancy[found_station];

            printf("[SECURITY] Pasażer(gender=%d) WYCHODZI z st.%d (occupancy=%d)\n",
                   gender, found_station, occ_after);

            if (occ_after == 0) {
                // Zwolnione całkowicie
                g_data.station_gender[found_station] = -1;
                printf("[SECURITY] Stanowisko %d PUSTE.\n", found_station);
            }
            pthread_mutex_unlock(&g_data.station_mutex);

            // Kończymy -> pasażer przeszedł kontrolę
            return;
        }

        // Nie znaleźliśmy stacji -> czekamy i ponawiamy
        sleep(1);
    }
}