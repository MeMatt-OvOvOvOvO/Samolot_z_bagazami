#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include "shared.h"

/* Tutaj definicja zmiennej globalnej */
struct global_data g_data;

pthread_mutex_t g_data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t station_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t hall_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t hall_not_empty_cond = PTHREAD_COND_INITIALIZER;

pthread_mutex_t boarding_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t boarding_cond = PTHREAD_COND_INITIALIZER;

hall_node *vip_head = NULL;
hall_node *vip_tail = NULL;
hall_node *normal_head = NULL;
hall_node *normal_tail = NULL;

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
        pthread_mutex_lock(&g_data_mutex);
        g_data.plane_start_earlier = 1;
        pthread_mutex_unlock(&g_data_mutex);
        fprintf(stderr, "[SIGNAL] Otrzymano SIGUSR1 -> start samolotu wcześniej.\n");
    }
}

/* Obsługa sygnału SIGUSR2 */
static void sigusr2_handler(int signo)
{
    if (signo == SIGUSR2) {
    	pthread_mutex_lock(&g_data_mutex);
    	g_data.stop_generating = 1;
    	pthread_mutex_unlock(&g_data_mutex);
        fprintf(stderr, "[SIGNAL] Otrzymano SIGUSR2 -> zamykam odprawę biletowo-bagażową!\n");
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

    struct sigaction sa2;
    memset(&sa2, 0, sizeof(sa2));
    sa2.sa_handler = sigusr2_handler;
    sigemptyset(&sa2.sa_mask);

    if (sigaction(SIGUSR2, &sa2, NULL) == -1) {
        perror("sigaction(SIGUSR2)");
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

    // Wybieramy odpowiednią kolejkę: VIP lub normal
    hall_node **head_ptr = is_vip ? &vip_head : &normal_head;
    hall_node **tail_ptr = is_vip ? &vip_tail : &normal_tail;

    // Jeśli lista jest pusta, ustawiamy head i tail na nowy węzeł
    if (*head_ptr == NULL) {
        *head_ptr = node;
        *tail_ptr = node;
    } else {
        // (sortowanie rosnące)
        hall_node *current = *head_ptr;
        hall_node *prev = NULL;
        while (current != NULL && current->passenger_id < passenger_id) {
            prev = current;
            current = current->next;
        }
        if (prev == NULL) {
            // Wstawiamy na początek
            node->next = *head_ptr;
            *head_ptr = node;
        } else {
            // Wstawiamy między prev a current
            prev->next = node;
            node->next = current;
            if (current == NULL) { // wstawienie na końcu
                *tail_ptr = node;
            }
        }
    }

    pthread_mutex_unlock(&hall_mutex);

//    printf("[DEBUG] enqueue_hall: Dodano pasażera %d (VIP=%d, bag=%d) do kolejki (sortowanej).\n",
//           passenger_id, is_vip, bag_weight);
    pthread_cond_signal(&hall_not_empty_cond);
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

/*******************************************************************
 * Pokazuje do konsoli ilość pasażerów w kolejce vip i normalnej
 *******************************************************************/
void print_hall_queues(void)
{
    int countVIP = 0;
    int countNor = 0;
    pthread_mutex_lock(&hall_mutex);

    hall_node *iter = vip_head;
    while (iter) {
        countVIP++;
        iter = iter->next;
    }
    iter = normal_head;
    while (iter) {
        countNor++;
        iter = iter->next;
    }

    pthread_mutex_unlock(&hall_mutex);
    printf("[HALL] Liczba pasażerów w kolejce VIP: %d, normalnej: %d\n",
           countVIP, countNor);
}

/*******************************************************
 * Sprawdzamy czy pasażer jest w holu
 *******************************************************/
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