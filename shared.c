#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "shared.h"

/* Tutaj definicja zmiennej globalnej */
struct global_data g_data;

/* Funkcja do czyszczenia semaforów */
void safe_sem_unlink(const char *name)
{
    if (sem_unlink(name) == -1 && errno != ENOENT) {
        fprintf(stderr, "sem_unlink(%s) error: %s\n", name, strerror(errno));
    }
}

/* Funkcja do wczytywania liczby całkowitej > 0 */
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

/* Obsługa sygnału SIGUSR1 */
static void sigusr1_handler(int signo)
{
    if (signo == SIGUSR1) {
        pthread_mutex_lock(&g_data.g_data_mutex);
        g_data.plane_start_earlier = 1;
        pthread_mutex_unlock(&g_data.g_data_mutex);
        fprintf(stderr, "[SIGNAL] Otrzymano SIGUSR1 -> start samolotu wcześniej.\n");
    }
}

/* Inicjalizacja obsługi sygnałów */
void setup_signals(void)
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

/*******************************************************
 * Funkcje obsługi "holu" - kolejka VIP + normal
 *******************************************************/
void enqueue_hall(int passenger_id, int is_vip, int bag_weight)
{
    hall_node *node = malloc(sizeof(hall_node));
    if (!node) {
        perror("malloc(hall_node)");
        return;
    }
    node->passenger_id = passenger_id;
    node->is_vip = is_vip;
    node->bag_weight = bag_weight;
    node->next = NULL;

    // generujemy nazwę semafora
    snprintf(node->sem_name, sizeof(node->sem_name), "/board_sem_%d", passenger_id);
    safe_sem_unlink(node->sem_name);
    node->board_sem = sem_open(node->sem_name, O_CREAT, 0666, 0);
    if (node->board_sem == SEM_FAILED) {
        perror("sem_open(node->board_sem)");
        free(node);
        return;
    }

    pthread_mutex_lock(&hall_mutex);

    if (is_passenger_in_hall(passenger_id)) {
        // Już jest w kolejce, nie dodajemy go ponownie
        pthread_mutex_unlock(&hall_mutex);
        return;
    }

    // Tworzymy nowy węzeł
    if (!node) {
        perror("malloc(hall_node)");
        pthread_mutex_unlock(&hall_mutex);
        return;
    }
    node->passenger_id = passenger_id;
    node->is_vip = is_vip;
    node->bag_weight = bag_weight;
    node->next = NULL;

    // Dodajemy do odpowiedniej listy
    if (is_vip) {
        // VIP -> na początek
        node->next = vip_head;
        vip_head = node;
        if (!vip_tail) {
            vip_tail = node;
        }
    } else {
        // Normal -> na koniec
        if (!normal_head) {
            normal_head = node;
            normal_tail = node;
        } else {
            normal_tail->next = node;
            normal_tail = node;
        }
    }
    pthread_mutex_unlock(&hall_mutex);
}

/* Pobiera pasażera z kolejki holu:
   - Najpierw sprawdź VIP (od "głowy"),
   - jeśli pusto -> normal
   Zwraca węzeł, albo NULL jeśli obie puste.
*/
hall_node *dequeue_hall(void)
{
    pthread_mutex_lock(&hall_mutex);
    hall_node *res = NULL;
    if (vip_head) {
        res = vip_head;
        vip_head = vip_head->next;
        if (!vip_head) vip_tail = NULL;
    } else if (normal_head) {
        res = normal_head;
        normal_head = normal_head->next;
        if (!normal_head) normal_tail = NULL;
    }
    pthread_mutex_unlock(&hall_mutex);
    return res;
}

void print_hall_queues(void)
{
    pthread_mutex_lock(&hall_mutex);

    printf("[HALL] VIP queue: ");
    hall_node *iter = vip_head;
    if (!iter) {
        printf("(pusto)");
    }
    while (iter) {
        printf(" -> P%d(VIP=%d, bw=%d)", iter->passenger_id, iter->is_vip, iter->bag_weight);
        iter = iter->next;
    }
    printf("\n");

    printf("[HALL] Normal queue: ");
    iter = normal_head;
    if (!iter) {
        printf("(pusto)");
    }
    while (iter) {
        printf(" -> P%d(VIP=%d, bw=%d)", iter->passenger_id, iter->is_vip, iter->bag_weight);
        iter = iter->next;
    }
    printf("\n");

    pthread_mutex_unlock(&hall_mutex);
}

int is_passenger_in_hall(int pid)
{
    // Sprawdzamy listę VIP
    hall_node *iter = vip_head;
    while (iter) {
        if (iter->passenger_id == pid) {
            return 1; // znaleziony
        }
        iter = iter->next;
    }
    // Sprawdzamy listę normal
    iter = normal_head;
    while (iter) {
        if (iter->passenger_id == pid) {
            return 1;
        }
        iter = iter->next;
    }
    return 0; // nie znaleziony
}