#include <glib.h>
#include <sys/syscall.h>
#include <time.h>
#include <stdio.h>

#include "profile_utils.h"

#define TIME_REC_PATH "/mnt/shared/res.log"

#define gettid() ((pid_t)syscall(SYS_gettid))

struct thread_time_recorder_s
{
    int tid;
    int num_l;   // line number, init to 0
    int num_inl; // item index in line
    int total;
    GArray *l_list; // lines list, store the item counts for each line
    GArray *i_list; // all items
};
typedef struct thread_time_recorder_s thread_time_recorder_t;

// struct time_recorder_s
// {
GHashTable *thread_tr_list;
pthread_mutex_t print_lock;
// };
// typedef struct time_recorder_s time_recorder_t;

void init_time_rec()
{
#ifdef SS_DEBUG
    printf("[TIME REC] time recorder init\n");
#endif
    thread_tr_list = g_hash_table_new(g_direct_hash, g_direct_equal);
    pthread_mutex_init(&print_lock, NULL);
    return;
}

void fini_time_rec()
{
    // TODO: finalize
}

static thread_time_recorder_t *new_thread_tr()
{
    int tid = gettid();
#ifdef SS_DEBUG
    printf("[TIME REC] new tr list for tid %d\n", tid);
#endif
    thread_time_recorder_t *tr;
    tr = (thread_time_recorder_t *)malloc(sizeof(thread_time_recorder_t));
    tr->tid = tid;
    tr->num_l = tr->num_inl = tr->total = 0;
    tr->l_list = g_array_new(FALSE, TRUE, sizeof(int));
    tr->i_list = g_array_new(FALSE, TRUE, sizeof(struct timespec));
    g_hash_table_insert(thread_tr_list, GINT_TO_POINTER(tid), tr);
    return tr;
}

void time_rec_add()
{
    int tid = gettid();
#ifdef SS_DEBUG
    printf("[TIME REC] add record to tid %d\n", tid);
#endif
    thread_time_recorder_t *tr = g_hash_table_lookup(thread_tr_list, GINT_TO_POINTER(tid));
    if (tr == NULL)
    {
        tr = new_thread_tr();
    }
    tr->total++;
    tr->num_inl++;

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    g_array_append_val(tr->i_list, time);
    return;
}

void time_rec_newline()
{
    int tid = gettid();
    thread_time_recorder_t *tr = g_hash_table_lookup(thread_tr_list, GINT_TO_POINTER(tid));
    // better not to call this first
    if (tr == NULL)
    {
        tr = new_thread_tr();
    }
    g_array_append_val(tr->l_list, tr->num_inl);
    tr->num_inl = 0;
    tr->num_l++;
    return;
}

void time_rec_print()
{
    int tid = gettid();
    thread_time_recorder_t *tr = g_hash_table_lookup(thread_tr_list, GINT_TO_POINTER(tid));
    pthread_mutex_lock(&print_lock);
    printf("--- time recorder: tid %d ---\n", tid);
    int i = 0;
    int l = 0;
    int inl = 0;
    while (i < tr->total)
    {
        if (inl == g_array_index(tr->l_list, int, l))
        {
            l++;
            inl = 0;
            printf("\n");
        }
        struct timespec ts = g_array_index(tr->i_list, struct timespec, i);
        printf("%lld\t", ts.tv_sec * 1000000000L + ts.tv_nsec);

        inl++;
        i++;
    }
    pthread_mutex_unlock(&print_lock);
    return;
}

static void __tr_save2file(char* fname, thread_time_recorder_t *tr)
{
#ifdef SS_DEBUG
    printf("[TIME REC] tr saving tid %d to file %s\n", tr->tid, fname);
#endif
    FILE *savefile = fopen(fname, "a+");

    char dout[65536];
#define out &dout
    pthread_mutex_lock(&print_lock);

    snprintf(out, 45, "--- time recorder: tid %d ---\n", tr->tid);
    fwrite(out, sizeof(char), strlen(out)+1, savefile);

    int i = 0;
    int l = 0;
    int inl = 0;
    while (i < tr->total)
    {
        if (inl == g_array_index(tr->l_list, int, l))
        {
            l++;
            inl = 0;
            snprintf(out, strlen("\n")+1, "\n");
            fwrite(out, sizeof(char), strlen(out)+1, savefile);
        }
        struct timespec ts = g_array_index(tr->i_list, struct timespec, i);
        snprintf(out, 128, "%lld\t", ts.tv_sec * 1000000000 + ts.tv_nsec);
        fwrite(out, sizeof(char), strlen(out)+1, savefile);

        inl++;
        i++;
    }
    pthread_mutex_unlock(&print_lock);
    free(out);
#ifdef SS_DEBUG
    printf("[TIME REC] tr file %s for tid %d saved\n", fname, tr->tid);
#endif
    return;
}

void time_rec_savefile()
{
    int tid = gettid();
    thread_time_recorder_t *tr = g_hash_table_lookup(thread_tr_list, GINT_TO_POINTER(tid));
    __tr_save2file(TIME_REC_PATH, tr);
    return;
}

void __iter_time_recorders(gpointer key, gpointer value, gpointer data)
{
#ifdef SS_DEBUG
    printf("[TIME REC] tr iter\n");
#endif
    thread_time_recorder_t *tr = (thread_time_recorder_t *)value;
    __tr_save2file(TIME_REC_PATH, tr);
    return;
}

void time_rec_savefile_all()
{
#ifdef SS_DEBUG
    printf("[TIME REC] tr save all start\n");
#endif
    int sz = g_hash_table_size(thread_tr_list);
#ifdef SS_DEBUG
    printf("[TIME REC] tr tid table size: %d\n", sz);
#endif
    g_hash_table_foreach(thread_tr_list, __iter_time_recorders, NULL);
#ifdef SS_DEBUG
    printf("[TIME REC] tr save all end\n");
#endif
    return;
}