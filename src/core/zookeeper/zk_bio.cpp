/* refer: https://github.com/redis/redis/blob/unstable/src/bio.c */

#include "zk_bio.h"

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "util/util.h"
#include "zk_task.h"

namespace kim {

/* Make sure we have enough stack to perform all the things we do 
 * in the main thread. */
#define REDIS_THREAD_STACK_SIZE (1024 * 1024 * 4)

Bio::Bio(Log* logger) : m_logger(logger) {
}

Bio::~Bio() {
    pthread_mutex_lock(&m_mutex);
    for (auto& v : m_req_tasks) delete v;
    for (auto& v : m_ack_tasks) delete v;
    m_req_tasks.clear();
    m_ack_tasks.clear();
    pthread_mutex_unlock(&m_mutex);
}

bool Bio::bio_init() {
    pthread_attr_t attr;
    pthread_t thread;
    size_t stacksize;

    pthread_mutex_init(&m_mutex, NULL);
    pthread_cond_init(&m_cond, NULL);
    /* Set the stack size as by default it may be small in some system */
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &stacksize);
    if (!stacksize) stacksize = 1; /* The world is full of Solaris Fixes */
    while (stacksize < REDIS_THREAD_STACK_SIZE) stacksize *= 2;
    pthread_attr_setstacksize(&attr, stacksize);

    /* Ready to spawn our threads. We use the single argument the thread
     * function accepts in order to pass the job ID the thread is
     * responsible of. */
    if (pthread_create(&thread, &attr, bio_process_tasks, this) != 0) {
        LOG_ERROR("Fatal: Can't initialize Background Jobs.");
        return false;
    }
    m_thread = thread;
    return true;
}

bool Bio::add_cmd_task(const std::string& path, zk_task_t::CMD cmd, const std::string& value) {
    zk_task_t* task = new zk_task_t{path, value, cmd, time_now()};
    if (task == nullptr) {
        LOG_ERROR("new task failed! path: %s", path.c_str());
        return false;
    }

    pthread_mutex_lock(&m_mutex);
    m_req_tasks.push_back(task);
    pthread_cond_signal(&m_cond);
    pthread_mutex_unlock(&m_mutex);
    return true;
}

void* Bio::bio_process_tasks(void* arg) {
    Bio* bio = (Bio*)arg;

    sigset_t sigset;
    /* Make the thread killable at any time, so that bioKillThreads()
     * can work reliably. */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* Block SIGALRM so we are sure that only the main thread will
     * receive the watchdog signal. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL)) {
        // LOG_WARN("Warning: can't mask SIGALRM in bio.c thread: %s", strerror(errno));
    }

    while (!bio->m_stop_thread) {
        zk_task_t* task = nullptr;

        pthread_mutex_lock(&bio->m_mutex);
        while (bio->m_req_tasks.size() == 0) {
            /* wait for pthread_cond_signal. */
            pthread_cond_wait(&bio->m_cond, &bio->m_mutex);
        }
        task = bio->m_req_tasks.front();
        bio->m_req_tasks.pop_front();
        pthread_mutex_unlock(&bio->m_mutex);

        if (task != nullptr) {
            bio->bio_process_cmd(task);
            bio->add_ack_task(task);
        }
    }

    return nullptr;
}

void Bio::add_ack_task(zk_task_t* task) {
    pthread_mutex_lock(&m_mutex);
    m_ack_tasks.push_back(task);
    pthread_mutex_unlock(&m_mutex);
}

void Bio::on_repeat_timer() {
    /* acks */
    handle_acks();
}

void Bio::handle_acks() {
    int i = 0;
    std::list<zk_task_t*> tasks;

    /* fetch 100 acks to handle. */
    pthread_mutex_lock(&m_mutex);
    while (m_ack_tasks.size() > 0 && i++ < 100) {
        tasks.push_back(m_ack_tasks.front());
        m_ack_tasks.pop_front();
    }
    pthread_mutex_unlock(&m_mutex);

    for (auto& v : tasks) {
        timer_process_ack(v);
        SAFE_DELETE(v);
    }
}

}  // namespace kim
