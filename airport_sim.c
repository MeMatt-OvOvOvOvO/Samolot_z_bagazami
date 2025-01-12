/*******************************************************
 * Program przyjmuje jeden parametr:
 *   ./airport_sim
 *       (Program zapyta o kolejne parametry.)
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
#include <signal.h>

#define SECURITY_STATIONS 3

struct {
    int total_passengers;
    int generated_count;
    int finished_passengers;

    int is_simulation_active;

    int baggage_limit;
    int stairs_capacity;

    int takeoff_time;
    int plane_capacity;
    int people_in_plane;

    int plane_start_earlier;  // 0 = nie, 1 = tak

    /* Flaga: 0 = można wsiadać do bieżącego samolotu
     *        1 = samolot startuje, spóźnieni czekają na nowy
     */
    int plane_in_flight;

    sem_t *baggage_check_sem;
    sem_t *stairs_sem; // Semafor nazwany do schodów pasażerskich

    /* TABLICA WSKAŹNIKÓW do semaforów nazwanych (3 stanowiska).
     * Każdy semafor ma początkową wartość = 2. */
    sem_t *security_sem[SECURITY_STATIONS];


    int station_gender[SECURITY_STATIONS]; // -1=puste, 0=m,1=f
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

void enter_security_check(int gender, int is_vip);
void enter_stairs_and_plane(int id);

/*******************************************************
 * Funkcja do ignorowania ENOENT w sem_unlink()
 *******************************************************/
static void safe_sem_unlink(const char *name)
{
    if (sem_unlink(name) == -1 && errno != ENOENT) {
        // tylko jeśli błąd != brak pliku
        fprintf(stderr, "sem_unlink(%s) error: %s\n", name, strerror(errno));
    }
}

/*******************************************************
 * Handler sygnału SIGUSR1 (start samolotu wcześniej)
 *******************************************************/
static void sigusr1_handler(int signo)
{
    if (signo == SIGUSR1) {
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_start_earlier = 1;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        fprintf(stderr, "[SIGNAL] Otrzymano SIGUSR1 -> start samolotu wcześniej.\n");
    }
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction(SIGUSR1)");
        exit(EXIT_FAILURE);
    }
}

int get_positive_int(const char *prompt)
{
    while (1) {
        int val;
        int ret;

        printf("%s", prompt);
        ret = scanf("%d", &val);
        if (ret != 1 || val <= 0) {
            fprintf(stderr, "Błąd: Wpisz liczbę całkowitą dodatnią.\n");
            while (getchar() != '\n') { /* czyszczenie bufora */ }
        } else {
            return val;
        }
    }
}

int main(void)
{
    // 1) Interaktywny input w menux
    printf("=== KONFIGURACJA SYMULACJI ===\n");

    g_data.total_passengers = get_positive_int("Podaj liczbę pasażerów: ");
    g_data.baggage_limit = get_positive_int("Podaj limit bagażu (kg): ");
    g_data.stairs_capacity = get_positive_int("Podaj pojemność schodów pasażerskich (K): ");
    g_data.takeoff_time = get_positive_int("Podaj czas (sek) po którym samolot odlatuje (T1): ");
    g_data.plane_capacity = get_positive_int("Podaj pojemność samolotu (P): ");


    g_data.generated_count = 0;
    g_data.finished_passengers = 0;
    g_data.is_simulation_active = 1;
    g_data.people_in_plane = 0;
    g_data.plane_in_flight = 0;
    g_data.plane_start_earlier = 0;

    setup_signals();

    /* Inicjujemy losowosc */
    srand(time(NULL));

    sem_unlink("/baggage_check_sem"); // gdyby poprzedni semafor nie byl usuniety
    g_data.baggage_check_sem = sem_open("/baggage_check_sem", O_CREAT, 0666, 1);
    if (g_data.baggage_check_sem == SEM_FAILED) {
        perror("sem_open(baggage_check_sem) error");
        return EXIT_FAILURE;
    }

    /* Semafor nazwany do schodow o pojemności K */
    sem_unlink("/stairs_sem");
    g_data.stairs_sem = sem_open("/stairs_sem", O_CREAT, 0666, g_data.stairs_capacity);
    if (g_data.stairs_sem == SEM_FAILED) {
        perror("sem_open(stairs_sem)");
        return EXIT_FAILURE;
    }

    /* Semafory nazwane do 3 stanowisk bezpieczeństwa */
    for (int i = 0; i < SECURITY_STATIONS; i++) {
        char name[32];
        sprintf(name, "/security_sem_%d", i);

        sem_unlink(name); // na wypadek, gdyby istniał
        g_data.security_sem[i] = sem_open(name, O_CREAT, 0666, 2);
        if (g_data.security_sem[i] == SEM_FAILED) {
            perror("sem_open(security_sem[i])");
            return EXIT_FAILURE;
        }

        g_data.station_gender[i] = -1; // puste
        g_data.station_occupancy[i] = 0;  // brak osób
    }

    /* Inicjujemy muteksy */
    if (pthread_mutex_init(&g_data.station_mutex, NULL) != 0) {
        perror("pthread_mutex_init(station_mutex)");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_init(&g_data.g_data_mutex, NULL) != 0) {
        perror("pthread_mutex_init(g_data_mutex)");
        exit(EXIT_FAILURE);
    }

    printf("[MAIN] Start symulacji. Passengers=%d, limit=%d, stairs=%d, T1=%d, planeCap=%d\n",
           g_data.total_passengers, g_data.baggage_limit, g_data.stairs_capacity,
           g_data.takeoff_time, g_data.plane_capacity);

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
     * - niszczymy semafory
     * - niszczymy muteksy
     */
    // Odprawa biletowo-bagażowa
    if (sem_close(g_data.baggage_check_sem) != 0) {
        perror("sem_close(baggage_check_sem)");
    }
    safe_sem_unlink("/baggage_check_sem");

    // stairs
    if (sem_close(g_data.stairs_sem) != 0) {
        perror("sem_close(stairs_sem)");
    }
    safe_sem_unlink("/stairs_sem");

    // security
    for (int i = 0; i < SECURITY_STATIONS; i++) {
        if (sem_close(g_data.security_sem[i]) != 0) {
            perror("sem_close(security_sem[i])");
        }
        char name[32];
        sprintf(name, "/security_sem_%d", i);
        safe_sem_unlink(name);
    }

    pthread_mutex_destroy(&g_data.station_mutex);
    pthread_mutex_destroy(&g_data.g_data_mutex);

    printf("[MAIN] Symulacja zakończona.\n");
    return EXIT_SUCCESS;
}

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
        if (finished >= total) {
            pthread_mutex_lock(&g_data.g_data_mutex);
            g_data.is_simulation_active = 0;
            pthread_mutex_unlock(&g_data.g_data_mutex);
            printf("[DISPATCHER] Wszyscy pasażerowie (%d) zakończyli.\n", finished);
            break;
        }

        if (!still_active) {
            printf("[DISPATCHER] is_simulation_active=0 -> kończę.\n");
            break;
        }
    }

    printf("[DISPATCHER] Kończę wątek.\n");
    pthread_exit(NULL);
}

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

        // Czekamy do upływu T1 lub do wypełnienia
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

void *passenger_generator_thread(void *arg)
{
    (void) arg;

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

void *passenger_thread(void *arg)
{
    int my_id = *((int *)arg);
    free(arg); // zwalniamy pamiec zaalokowana w generatorze

    int bag_weight = rand() % 10 + 1; // 1-10 kg
    int is_vip = (rand() % 5 == 0) ? 1 : 0; // co piaty VIP
    int gender = (rand() % 2); // 0 = mezczyzna, 1 = kobieta

    printf("[PASSENGER %d] Jestem watkiem pasazera (bagaz=%d kg, VIP=%d, gender=%d)\n",
           my_id, bag_weight, is_vip, gender);

    /**** Odprawa bagażowa ****/
    if (sem_wait(g_data.baggage_check_sem) != 0) {
        perror("sem_wait(baggage_check_sem)");
        goto finish_passenger;
    }
    printf("[PASSENGER %d] Odprawa bagażowa...\n", my_id);
    sleep(1);

    if (bag_weight > g_data.baggage_limit) {
        printf("[PASSENGER %d] Odrzucony (bagaż %d > %d)\n", my_id, bag_weight, g_data.baggage_limit);
        sem_post(g_data.baggage_check_sem);
        goto finish_passenger;
    }
    printf("[PASSENGER %d] Odprawa OK.\n", my_id);
    sem_post(g_data.baggage_check_sem);

    /**** Kontrola bezpieczeństwa ****/
    enter_security_check(gender, is_vip);

    /**** Schody -> Samolot ****/
    enter_stairs_and_plane(my_id);

    printf("[PASSENGER %d] W samolocie, czeka na odlot.\n", my_id);
    sleep(1);

finish_passenger:
    // Sygnalizujemy, że pasażer skończył (niezależnie od wyniku)
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
void enter_security_check(int gender, int is_vip)
{
    while (1) {
        int found_station = -1;

        pthread_mutex_lock(&g_data.station_mutex);

        // *** VIP najpierw ***
        if (is_vip) {
            // Najpierw spróbujmy znaleźć jakiekolwiek wolne stanowisko (nawet, jeśli stoi tam inna płeć, ale ma occupancy=0)
            // lub takie z tą samą płcią i occupancy=1
            for (int i = 0; i < SECURITY_STATIONS; i++) {
                if (g_data.station_occupancy[i] < 2) {
                    // jeśli puste (gender=-1) lub zgodny gender i occupancy=1
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

        // jeśli nie udało się VIP-owi w powyższym trybie,
        // albo to nie VIP (is_vip=0), stosujemy normalną logikę
        if (found_station < 0) {
            // standardowa pętla
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
            // mamy stację
            if (sem_wait(g_data.security_sem[found_station]) != 0) {
                perror("sem_wait(security_sem)");
                continue;
            }

            pthread_mutex_lock(&g_data.station_mutex);
            g_data.station_occupancy[found_station]++;
            int occ_now = g_data.station_occupancy[found_station];
            pthread_mutex_unlock(&g_data.station_mutex);

            if (is_vip) {
                printf("[SECURITY] [VIP] Pasażer(gender=%d) WCHODZI st.%d (occ=%d)\n",
                       gender, found_station, occ_now);
            } else {
                printf("[SECURITY] Pasażer(gender=%d) WCHODZI st.%d (occ=%d)\n",
                       gender, found_station, occ_now);
            }

            sleep(1); // kontrola

            if (sem_post(g_data.security_sem[found_station]) != 0) {
                perror("sem_post(security_sem)");
            }

            pthread_mutex_lock(&g_data.station_mutex);
            g_data.station_occupancy[found_station]--;
            int occ_after = g_data.station_occupancy[found_station];
            if (occ_after == 0) {
                g_data.station_gender[found_station] = -1;
                printf("[SECURITY] Stanowisko %d PUSTE.\n", found_station);
            }
            pthread_mutex_unlock(&g_data.station_mutex);

            if (is_vip) {
                printf("[SECURITY] [VIP] Pasażer(gender=%d) WYCHODZI st.%d (occ=%d)\n",
                       gender, found_station, occ_after);
            } else {
                printf("[SECURITY] Pasażer(gender=%d) WYCHODZI st.%d (occ=%d)\n",
                       gender, found_station, occ_after);
            }

            return;
        }

        // Brak pasującej stacji, czekamy
        sleep(1);
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
        // Sprawdzamy stan samolotu
        pthread_mutex_lock(&g_data.g_data_mutex);
        int in_flight = g_data.plane_in_flight;
        pthread_mutex_unlock(&g_data.g_data_mutex);

        if (in_flight == 1) {
            // Samolot startuje -> pasażer czeka w holu
            printf("[HALL] Pasażer %d czeka, bo samolot jest w locie...\n", id);
            sleep(1);
            continue;
        }
        // in_flight=0 => samolot dostępny -> możemy przejść do schodów
        break;
    }

    // Teraz wchodzimy na schody i samolot
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

    // Po schodach -> samolot
    pthread_mutex_lock(&g_data.g_data_mutex);
    g_data.people_in_plane++;
    int now_in_plane = g_data.people_in_plane;
    pthread_mutex_unlock(&g_data.g_data_mutex);

    printf("[PLANE] Pasażer %d zajął miejsce w samolocie (inPlane=%d)\n", id, now_in_plane);
}