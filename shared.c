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
void enqueue_hall(int passenger_id, int is_vip)
{
    hall_node *node = malloc(sizeof(hall_node));
    if (!node) {
        perror("malloc(hall_node)");
        return;
    }
    node->passenger_id = passenger_id;
    node->is_vip = is_vip;
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
    if (is_vip) {
        // VIP -> na początek listy VIP
        if (!vip_head) {
            vip_head = node;
            vip_tail = node;
        } else {
            node->next = vip_head;
            vip_head = node;
        }
        printf("[HALL] Pasażer %d (VIP) dodany do holu.\n", passenger_id);
    } else {
        // normal -> na koniec listy normal
        if (!normal_head) {
            normal_head = node;
            normal_tail = node;
        } else {
            normal_tail->next = node;
            normal_tail = node;
        }
        printf("[HALL] Pasażer %d (normal) dodany do holu.\n", passenger_id);
    }
    pthread_mutex_unlock(&hall_mutex);
}

/* Pobierz pasażera z kolejki holu:
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