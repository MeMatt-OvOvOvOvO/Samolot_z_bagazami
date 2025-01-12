#ifndef PASSENGER_H
#define PASSENGER_H

/* Watek generatora pasazerow - tworzy watki pasazerow */
void *passenger_generator_thread(void *arg);

/* Kazdy Pasazer tez bedzie watkiem */
void *passenger_thread(void *arg);

#endif