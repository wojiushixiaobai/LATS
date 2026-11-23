/* Experimental HAMT bridge: create pthreads in the host, never via clone3
 * from a KVM guest. The guest remains blocked until pthread_create returns. */
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
bool hamt_is_running(void);

typedef struct Job {
    pthread_t *thread;
    const pthread_attr_t *attr;
    void *(*start)(void *);
    void *arg;
    sigset_t mask;
    int result;
    bool done;
    struct Job *next;
} Job;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t incoming = PTHREAD_COND_INITIALIZER;
static pthread_cond_t finished = PTHREAD_COND_INITIALIZER;
static Job *first, **last = &first;
static bool ready;
static pthread_once_t helper_once = PTHREAD_ONCE_INIT;
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
void reims_thread_helper_init(void);
static void *service(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&lock);
        while (!first) pthread_cond_wait(&incoming, &lock);
        Job *j = first;
        first = j->next;
        if (!first) last = &first;
        pthread_mutex_unlock(&lock);
        sigset_t previous;
        int error = pthread_sigmask(SIG_SETMASK, &j->mask, &previous);
        if (!error) {
            error = __real_pthread_create(j->thread, j->attr, j->start, j->arg);
            pthread_sigmask(SIG_SETMASK, &previous, NULL);
        }
        pthread_mutex_lock(&lock);
        j->result = error;
        j->done = true;
        pthread_cond_broadcast(&finished);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}
static void start_helper(void)
{
    pthread_t t;
    int e = __real_pthread_create(&t, NULL, service, NULL);
    if (e) { fprintf(stderr, "reims host thread helper: %d\n", e); abort(); }
    pthread_detach(t);
    ready = true;
}
void reims_thread_helper_init(void)
{
    pthread_once(&helper_once, start_helper);
}
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    if (!hamt_is_running()) return __real_pthread_create(thread, attr, start, arg);
    if (!ready) return EAGAIN;
    Job j = { .thread=thread, .attr=attr, .start=start, .arg=arg };
    int e = pthread_sigmask(SIG_SETMASK, NULL, &j.mask);
    if (e) return e;
    pthread_mutex_lock(&lock);
    *last = &j; last = &j.next;
    pthread_cond_signal(&incoming);
    while (!j.done) pthread_cond_wait(&finished, &lock);
    pthread_mutex_unlock(&lock);
    return j.result;
}
