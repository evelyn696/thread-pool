#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "thread_pool.h"
#include "arena.h"


static void *pipe_in_to_q1(void *arg);
static void *process_q1(void *arg);
static void *pipe_q1_to_q2(void *arg);
static void *process_q2(void *arg);



typedef struct {
	
	int nTasks;
	hts_tpool *pool;
	hts_tpool_process *q1;
	hts_tpool_process *q2;
    pthread_mutex_t *lock;
	
} pipe_options;



typedef struct {
	
	int id;
	pipe_options *opt;
	
} pipe_job;



static void *pipe_in_to_q1(void *arg) {
	pipe_options *opt = (pipe_options *)arg;
	for(int i=1; i<=opt->nTasks; i++) {
		pipe_job *job = malloc(sizeof(pipe_job));
		assert(job != 0);
		job->opt = opt;
		job->id = i;
		if(hts_tpool_dispatch(opt->pool, opt->q1, process_q1, job) != 0) {
			free(job);
			pthread_exit((void*)1);
		}
	}
	pthread_exit(0);
}



static void *process_q1(void *arg) {
	pipe_job *job = (pipe_job *)arg;
	printf("Processing task %d in queue1\n", job->id);
	// free any job-related memory structs no longer in use
	return (void *)job;
}



static void *pipe_q1_to_q2(void *arg) {
	hts_tpool_result *result;
	pipe_options *opt = (pipe_options *)arg;
	while((result = hts_tpool_next_result_wait(opt->q1))) {
		pipe_job *job = (pipe_job *)hts_tpool_result_data(result);
		hts_tpool_delete_result(result, 0);
		if(hts_tpool_dispatch(opt->pool, opt->q2, process_q2, job) != 0) {
			pthread_exit((void*)1);
		}
		if(hts_tpool_process_empty(opt->q1)) {
			// free any option-related memory structs no longer in use
			break;
		}
	}
	pthread_exit(0);
}



static void *process_q2(void *arg) {
	pipe_job *job = (pipe_job *)arg;
	printf("Processing task %d in queue2\n", job->id);
	// free any job-related memory structs no longer in use
	free(job);
	return NULL;
}



int main(int argc, char *argv[]) {
	
	int ret;
	void *retv;
	pthread_mutex_t lock;
	pthread_t tidIto1, tid1to2;
	
	int nTasks = (int)strtol(argv[1], (char**)NULL, 10);
	int nThreads = (int)strtol(argv[2], (char**)NULL, 10);
	
	pthread_mutex_init(&lock, NULL);
	hts_tpool *p = hts_tpool_init(nThreads);
	hts_tpool_process *q1 = hts_tpool_process_init(p, nTasks, 0);
	hts_tpool_process *q2 = hts_tpool_process_init(p, nTasks, 0);
	pipe_options o = {nTasks, p, q1, q2, &lock};
	
	// Launch data source and sink threads
	pthread_create(&tidIto1, NULL, pipe_in_to_q1, &o);
	pthread_create(&tid1to2, NULL, pipe_q1_to_q2, &o);
	
	// Wait for source and sink threads to finish
	ret = 0;
	pthread_join(tidIto1, &retv); ret |= (retv != NULL);
	pthread_join(tid1to2, &retv); ret |= (retv != NULL);
	
	// Clean up
	hts_tpool_process_destroy(q1);
	hts_tpool_process_flush(q2);
	hts_tpool_process_destroy(q2);
	hts_tpool_destroy(p);
	pthread_mutex_destroy(&lock);
	
	// Return
	printf("Return value: %d\n", ret);
	return ret;
}

