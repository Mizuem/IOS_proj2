//IOS – projekt 2 (synchronizace)
//Autor: Denys Malytskyi (xmalytd00)

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <sys/shm.h>

#define N 10
#define MAX_L 20000
#define MAX_TL 10000
#define MAX_TB 1000
#define MAX_K 100
#define MIN_K 10

/*
	action_counter to numerate prints
	riders[N] to track skiers on stops
	goint_to_board to track how many skiers haven't riden the bus yet
	on_board to track how many skiers are in the bus
	*bus_stops semaphores for skiers to wait for the bus on the i stop
	*mutex semaphore for skiers to wait until bus reaches final stop
	*multiplex semaphore for skiers to not enter a full bus
	*bus semaphore for bus to take off from the final stop
	*allAboard semaphore for bus to take off from the stop once everybody is inside
	*actionSem semaphore to synchronize the prints 
	
*/

typedef struct {
    int action_counter;
	int riders[N];
	int going_to_board;
	int on_board;
} SharedData;

sem_t *bus_stops;
sem_t *mutex, *multiplex, *bus, *allAboard, *actionSem;

FILE *file;

SharedData *shared;

//A function to initiate semaphores and shared parameters
void init_params(int K, int L){
	//initiate semaphores
	mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	multiplex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	bus = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	allAboard = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	actionSem = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	bus_stops = mmap(NULL, sizeof(sem_t) * N, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	
    if (mutex == MAP_FAILED     || 
		multiplex == MAP_FAILED || 
		bus == MAP_FAILED 	    || 
		allAboard == MAP_FAILED) {
        fprintf(stderr, "ErrmmMap\n");
        exit(1);
    }
	
	
    if (sem_init(mutex, 1, 0) 	  == -1 || 
		sem_init(multiplex, 1, K) == -1 || 
		sem_init(bus, 1, 0) 	  == -1 || 
		sem_init(allAboard, 1, 0) == -1 || 
		sem_init(actionSem, 1, 1) == -1) {
        fprintf(stderr, "ErrmmMap\n");
        exit(1);
    }
	
	for(int i=0; i < N; i++){
		if(sem_init(&bus_stops[i], 1, 0) == -1){
			fprintf(stderr, "Err\n");
		}
	}
	
	//initiate shared parameters
	shared = mmap(NULL, sizeof(SharedData), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED) {
        fprintf(file, "ErrMap\n");
        exit(1);
    }
	
	shared->action_counter = 1;
	shared->going_to_board=L;
	shared->on_board = 0;
	for(int i = 0; i < N; i++){
		shared->riders[i] = 0;
	}
}

//A function to clean the memory
void cleanup(){
	if(munmap(shared, sizeof(SharedData)) == -1){
		fprintf(stderr, "ErrxDDD\n");
		exit(1);
	}
	if(munmap(bus_stops, N * sizeof(sem_t)) == -1 || 
	   munmap(mutex, sizeof(sem_t)) == -1         || 
	   munmap(multiplex, sizeof(sem_t)) == -1     || 
	   munmap(bus, sizeof(sem_t)) == -1           || 
	   munmap(allAboard, sizeof(sem_t)) == -1) {
        fprintf(stderr, "ErrXDDD\n");
        exit(1);
    }
	fclose(file);
}

//Bus process logic
void bus_process(int Z, int TB){
	//bus process start
	sem_wait(actionSem);
	fprintf(file, "%d: BUS: started\n", shared->action_counter++);
	sem_post(actionSem);

	TB++;
	while(shared->going_to_board > 0){
		for(int idZ = 0; idZ < Z; idZ++){
			//road to the stop
			usleep(rand() % TB);
			//arrival to the stop
			sem_wait(actionSem);
			fprintf(file, "%d: BUS: arrived to %d\n", shared->action_counter++, (idZ+1));
			sem_post(actionSem);
			//checking if there is somebody waiting
			if(shared->riders[idZ] > 0){
				//signal to skiers to get in the bus
				sem_post(&bus_stops[idZ]);
				//wait until everybody on the stop gets inside
				sem_wait(allAboard);
			}
			//leave the stop after everybody gets inside
			sem_wait(actionSem);
			fprintf(file, "%d: BUS: leaving %d\n", shared->action_counter++, (idZ+1));
			sem_post(actionSem);
		}
		//final stop
		usleep(rand() % TB);
		
		sem_wait(actionSem);
		fprintf(file, "%d: BUS: arrived to final\n", shared->action_counter++);
		sem_post(actionSem);
		
		//signal to the skiers to get off the bus

		//waiting for skiers to get off the bus
		if(shared->on_board > 0){
			sem_post(mutex);
			sem_wait(bus);
		}
		
		//leave the final stop
		sem_wait(actionSem);
		fprintf(file, "%d: BUS: leaving final\n", shared->action_counter++);
		sem_post(actionSem);
	}
	//bus process end
	sem_wait(actionSem);
	fprintf(file, "%d: BUS: finish\n", shared->action_counter++);
	sem_post(actionSem);
}

//Skier process logic
void ski_process(int idL, int TL, int Z){
	unsigned int seed = time(NULL) ^ (getpid()<<16) ^ getpid();
	int idZ = rand_r(&seed) % Z;
	TL++;
	//skier process start
	sem_wait(actionSem);
	fprintf(file, "%d: L %d: started\n", shared->action_counter++, idL);
	sem_post(actionSem);
	
	//waiting before arriving to the stop
	usleep(rand() % TL);
	
	//arriving to the stop
	sem_wait(actionSem);
	fprintf(file, "%d: L %d: arrived to %d\n", shared->action_counter++, idL, (idZ+1));
	sem_post(actionSem);
	
	//multiplex sem to ensure that the skiers won't enter a full bus
	sem_wait(multiplex);
	
	//waiting for the bus to come 
	shared->riders[idZ]++;
	sem_wait(&bus_stops[idZ]);
	
	//boarding the bus
	fprintf(file, "%d: L %d: boarding\n", shared->action_counter++, idL);
	
	shared->riders[idZ]--;
	shared->going_to_board--;
	shared->on_board++;
	
	//signaling for others to come inside or for bus to take off
	if(shared->riders[idZ] == 0){
		sem_post(allAboard);
	}
	else{
		sem_post(&bus_stops[idZ]);
	}
	
	//waiting for the final stop
	sem_wait(mutex);
	shared->on_board--;
	sem_post(multiplex);
	//signal for others to come out
	if(shared->on_board > 0){
		sem_wait(actionSem);
		fprintf(file, "%d: L %d: going to ski\n", shared->action_counter++, idL);
		sem_post(actionSem);
		sem_post(mutex);
	}
	
	//signal for the bus to depart once everybody is outside
	if(shared->on_board == 0){
		sem_wait(actionSem);
		fprintf(file, "%d: L %d: going to ski\n", shared->action_counter++, idL);
		sem_post(actionSem);
		sem_post(bus);
	}
	
}

int main(int argc, char *argv[]){
	if((file=fopen("proj2.out", "w"))==NULL){
		fprintf(stderr, "Err.\n");
		return 1;
	}
    if(argc != 6) {
        fprintf(stderr, "Err\n");
        return 1;
	}

	setbuf(file, NULL);
	srand(time(NULL));
    
	int L = atoi(argv[1]);
    int Z = atoi(argv[2]);
    int K = atoi(argv[3]);
    int TL = atoi(argv[4]);
    int TB = atoi(argv[5]);
	if(L <= 0 	   || 
	   L > MAX_L   || 
	   Z <= 0 	   || 
	   Z > N 	   || 
	   K < MIN_K   ||
	   K > MAX_K   || 
	   TL < 0      || 
	   TL > MAX_TL || 
	   TB < 0 	   || 
	   TB > MAX_TB ) {
        fprintf(stderr, "Err\n");
        return 1;
    }
	
	init_params(K, L);
	
	//Creating bus and skiers processes
	pid_t bus_pid =	fork();
	if(bus_pid == 0){
		bus_process(Z, TB);
		exit(0);
	}
	if(bus_pid < 0){
		fprintf(stderr, "Err.XD\n");
		exit(1);
	}
	
	for(int i = 0; i < L; i++){
		pid_t ski_pid = fork();
		if(ski_pid == 0){
			
			ski_process(i+1, TL, Z);
			exit(0);
		}
		if(ski_pid < 0){
			shared->going_to_board--;
			fprintf(stderr, "Err.xDD\n");
			exit(1);
		}
	}
	
	//Waiting for child processes to end
	for(int i = 0; i < L+1 ; i++){
		wait(NULL);
	}
	
	cleanup();
	
	exit(0);
	
}