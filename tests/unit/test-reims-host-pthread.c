#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
static __thread bool simulated_guest;
bool hamt_is_running(void) { return simulated_guest; }
void reims_thread_helper_init(void);
int __wrap_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static atomic_int errors;
static void *child(void *p) { sigset_t m; pthread_sigmask(SIG_SETMASK, NULL, &m); if (!sigismember(&m,SIGUSR1)) atomic_fetch_add(&errors,1); return p; }
static void *producer(void *p) {
 sigset_t m; sigemptyset(&m);sigaddset(&m,SIGUSR1);pthread_sigmask(SIG_BLOCK,&m,NULL);
 simulated_guest=true;
 for(int i=0;i<100;i++){pthread_t t;void *r=NULL;int e=__wrap_pthread_create(&t,NULL,child,p);if(e){atomic_fetch_add(&errors,1);continue;}pthread_join(t,&r);if(r!=p)atomic_fetch_add(&errors,1);}
 return NULL;
}
int main(void) {pthread_t p[8];reims_thread_helper_init();for(long i=0;i<8;i++)if(pthread_create(&p[i],NULL,producer,(void*)(i+1)))abort();for(int i=0;i<8;i++)pthread_join(p[i],NULL);printf("800 concurrent creates: errors=%d\n",atomic_load(&errors));return atomic_load(&errors)!=0;}
