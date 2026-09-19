/* Core-local C++ destructor ownership. Never register unloadable code with the
 * process-wide atexit list. The native loader executes .fini_array before unmap. */
#include <cstdlib>
#include <pthread.h>

namespace
{
struct Destructor
{
    void (*callback)(void *);
    void *argument;
    void *dso;
    Destructor *next;
};
Destructor *pending = nullptr;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
} // namespace

extern "C"
{
    void *__dso_handle = &__dso_handle;

    int __cxa_atexit(void (*callback)(void *), void *argument, void *dso)
    {
        auto *entry = static_cast<Destructor *>(std::malloc(sizeof(Destructor)));
        if (!entry)
            return -1;
        entry->callback = callback;
        entry->argument = argument;
        entry->dso = dso;
        pthread_mutex_lock(&lock);
        entry->next = pending;
        pending = entry;
        pthread_mutex_unlock(&lock);
        return 0;
    }

    void __cxa_finalize(void *dso)
    {
        for (;;)
        {
            pthread_mutex_lock(&lock);
            Destructor **slot = &pending;
            while (*slot && dso && (*slot)->dso != dso)
                slot = &(*slot)->next;
            Destructor *entry = *slot;
            if (entry)
                *slot = entry->next;
            pthread_mutex_unlock(&lock);
            if (!entry)
                break;
            auto callback = entry->callback;
            void *argument = entry->argument;
            std::free(entry);
            callback(argument);
        }
    }
}

__attribute__((destructor)) static void finish_core()
{
    __cxa_finalize(nullptr);
    pthread_mutex_destroy(&lock);
}
