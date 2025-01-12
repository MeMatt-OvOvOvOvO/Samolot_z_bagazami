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