/* Opt-in native thread diagnostic: does this title's std::thread work at all?
 *
 * Why this exists. PPSSPP's ThreadManager::Init creates a pool of std::threads and
 * the console killed the title with a SIGSEGV on the first worker, inside
 * libkernel.sprx's own text (klog/run-PPSA99169-002106.log: fault address 0x70).
 * PPSSPP is a large thing to debug that in, so this probe asks the same question in
 * the smallest possible program: create N std::threads from the title, have each one
 * touch a thread_local variable (which the SDK compiles as emulated TLS, i.e. a call
 * into the title's __emutls_get_address) and an atomic, join them, and record how far
 * it got.
 *
 * The record is rewritten after every stage, so a crash leaves the last completed
 * stage on disk instead of an empty file. Nothing on a normal run reads or writes
 * this: it is armed by /app0/thread-test.txt and removes that file when it starts.
 *
 *   echo 32 > /data/homebrew/PPSA99169/thread-test.txt   (then launch the title)
 *   RETR /data/homebrew/PPSA99169/thread-test.json       (the result)
 */
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <sys/mman.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::atomic<unsigned> bodies_ran{0};
std::atomic<unsigned> tls_ok{0};

/* A namespace-scope and a function-local thread_local, which are the two forms the
 * emutls runtime has to place: the first is up for every thread, the second only for
 * the thread that first reaches it. */
thread_local unsigned tls_counter = 7;

struct Record
{
    std::string stage;
    unsigned requested = 0;
    unsigned created = 0;
    unsigned ran = 0;
    unsigned joined = 0;
    unsigned hardware = 0;
};

Record record;

void write_record()
{
    if (FILE *out = std::fopen("/app0/thread-test.json", "w"))
    {
        std::fprintf(out,
                     "{\"stage\":\"%s\",\"requested\":%u,\"created\":%u,\"ran\":%u,"
                     "\"joined\":%u,\"bodies\":%u,\"tls_ok\":%u,\"hardware\":%u}\n",
                     record.stage.c_str(), record.requested, record.created, record.ran,
                     record.joined, bodies_ran.load(), tls_ok.load(), record.hardware);
        std::fclose(out);
    }
    std::fprintf(stderr, "thread test: stage=%s requested=%u created=%u ran=%u joined=%u\n",
                 record.stage.c_str(), record.requested, record.created, record.ran, record.joined);
}

/* One worker body. The TLS access is the interesting part: it is what makes a new
 * thread call into the title's emulated-TLS runtime. */
void worker(unsigned index)
{
    static thread_local unsigned local_counter = 0;
    tls_counter = index;
    local_counter++;
    if (tls_counter == index && local_counter == 1)
        tls_ok.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bodies_ran.fetch_add(1);
}

/* PPSSPP's workers do two things this probe did not, and its crash lands right there:
 * they lock a std::mutex and then wait on a std::condition_variable, both members of a
 * heap-allocated context object (Common/Thread/ThreadManager.cpp, WorkerThreadFunc).
 * These slots are that shape, so each stage can be attributed. */
struct Slot
{
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> running{true};
    std::atomic<unsigned> done{0};
};

void mutex_worker(Slot *slot)
{
    for (unsigned i = 0; i < 100; ++i)
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
    }
    slot->done.fetch_add(1);
    bodies_ran.fetch_add(1);
}

void condition_worker(Slot *slot)
{
    std::unique_lock<std::mutex> lock(slot->mutex);
    /* A bounded wait, so a lost wakeup cannot hang the probe forever. */
    slot->condition.wait_for(lock, std::chrono::milliseconds(5), [slot] { return !slot->running; });
    slot->done.fetch_add(1);
    bodies_ran.fetch_add(1);
}

void run_slot_stage(const char *stage, unsigned count, void (*body)(Slot *))
{
    record.stage = stage;
    record.requested = count;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    write_record();

    auto *slot = new Slot();
    std::vector<std::thread> threads;
    threads.reserve(count);
    for (unsigned i = 0; i < count; ++i)
    {
        threads.emplace_back(body, slot);
        record.created = i + 1;
        write_record();
    }
    for (auto &thread : threads)
    {
        thread.join();
        record.joined++;
        write_record();
    }
    record.ran = slot->done.load();
    write_record();
    delete slot;
}

/* The hypothesis this stage exists for: PPSSPP's workers are started by libc++
 * through pthread_create with an entry point that lives in the core's ELF, which this
 * title's loader maps anonymously - unlike every earlier thread in this port, whose
 * entry point is in the title's own registered image. If the console's thread startup
 * needs the entry to belong to a known module, this is where that shows, so the stage
 * calls pthread_create directly with an entry point inside an anonymous executable
 * page: `xor eax, eax; ret`, which returns a null void* and nothing else. */
void run_anonymous_entry_stage()
{
    record.stage = "anon-entry";
    record.requested = 1;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    write_record();

    void *page = mmap(nullptr, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED)
    {
        record.stage = "anon-entry-mmap-failed";
        write_record();
        return;
    }
    /* xor eax, eax ; ret -- a void *(*)(void *) that returns null. */
    const unsigned char code[] = {0x31, 0xc0, 0xc3};
    std::memcpy(page, code, sizeof(code));
    if (mprotect(page, 0x4000, PROT_READ | PROT_EXEC) != 0)
    {
        record.stage = "anon-entry-mprotect-failed";
        write_record();
        munmap(page, 0x4000);
        return;
    }

    pthread_t thread;
    const int created =
        pthread_create(&thread, nullptr, reinterpret_cast<void *(*)(void *)>(page), nullptr);
    record.created = created == 0 ? 1 : 2; /* 2 means pthread_create failed */
    write_record();
    if (created == 0)
    {
        void *result = nullptr;
        pthread_join(thread, &result);
        record.joined = 1;
        record.ran = 1;
        write_record();
    }
    munmap(page, 0x4000);
}

/* Create `count` threads, join them, and return how many were created and joined. */
void run_stage(const char *stage, unsigned count)
{
    record.stage = stage;
    record.requested = count;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    tls_ok.store(0);
    write_record();

    std::vector<std::thread> threads;
    threads.reserve(count);
    for (unsigned i = 0; i < count; ++i)
    {
        threads.emplace_back(worker, i);
        record.created = i + 1;
        write_record();
    }
    record.ran = bodies_ran.load();
    write_record();
    for (auto &thread : threads)
    {
        thread.join();
        record.joined++;
        write_record();
    }
}
} // namespace

/* The last difference between this probe and PPSSPP's pool: PPSSPP's threads run code
 * that belongs to the *core*, which ps5_core_dlopen mapped, rather than code in the
 * title's own image. This stage loads the real core, takes one of its libretro exports
 * and calls it from a fresh thread - the smallest possible version of what a PPSSPP
 * worker does. */
extern "C"
{
    void *ps5_core_dlopen(const char *, int);
    void *ps5_core_dlsym(void *, const char *);
    int ps5_core_dlclose(void *);
}

namespace
{
std::atomic<unsigned> core_calls{0};

void *core_call_thread(void *symbol)
{
    auto info = reinterpret_cast<void (*)(void *)>(symbol);
    unsigned char storage[512] = {};
    info(storage);
    core_calls.fetch_add(1);
    return nullptr;
}
} // namespace

void run_core_thread_stage()
{
    record.stage = "core-thread";
    record.requested = 1;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    core_calls.store(0);
    write_record();

    void *core = ps5_core_dlopen("/app0/cores/ppsspp_libretro.so", 0);
    if (!core)
    {
        record.stage = "core-thread-dlopen-failed";
        write_record();
        return;
    }
    void *symbol = ps5_core_dlsym(core, "retro_get_system_info");
    if (!symbol)
    {
        record.stage = "core-thread-dlsym-failed";
        write_record();
        ps5_core_dlclose(core);
        return;
    }
    /* First on this thread, so a crash after this is about the new thread. */
    auto info = reinterpret_cast<void (*)(void *)>(symbol);
    unsigned char storage[512] = {};
    info(storage);
    record.stage = "core-thread-main-call-ok";
    write_record();

    pthread_t thread;
    const int created = pthread_create(&thread, nullptr, core_call_thread, symbol);
    record.created = created == 0 ? 1 : 2;
    write_record();
    if (created == 0)
    {
        pthread_join(thread, nullptr);
        record.joined = 1;
        record.ran = core_calls.load();
        write_record();
    }
    ps5_core_dlclose(core);
}

/* The hypothesis the crash dump points at: libkernel's thread startup looks the start
 * address up in its module table and dereferences the result without a null check, so
 * a thread whose *entry point* is in the core's anonymously mapped image dies where a
 * thread entered from the title's own image does not. retro_api_version is the
 * harmless core export to enter on: it returns 1 and touches nothing. */
void run_core_entry_stage()
{
    record.stage = "core-entry";
    record.requested = 1;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    write_record();

    void *core = ps5_core_dlopen("/app0/cores/ppsspp_libretro.so", 0);
    if (!core)
    {
        record.stage = "core-entry-dlopen-failed";
        write_record();
        return;
    }
    void *symbol = ps5_core_dlsym(core, "retro_api_version");
    if (!symbol)
    {
        record.stage = "core-entry-dlsym-failed";
        write_record();
        ps5_core_dlclose(core);
        return;
    }
    pthread_t thread;
    const int created =
        pthread_create(&thread, nullptr, reinterpret_cast<void *(*)(void *)>(symbol), nullptr);
    record.created = created == 0 ? 1 : 2;
    write_record();
    if (created == 0)
    {
        pthread_join(thread, nullptr);
        record.joined = 1;
        record.ran = 1;
        write_record();
    }
    ps5_core_dlclose(core);
}

/* Called through the anonymous page, so libkernel sees an anonymous return address. */
extern "C" int pthread_mutex_lock_wrapper(void *mutex)
{
    return pthread_mutex_lock(static_cast<pthread_mutex_t *>(mutex));
}

/* The crash dump's faulting code walks the frame-pointer chain, takes a return
 * address from an outer frame and looks it up in the module table, then reads a field
 * of the result without checking for null. Everything in the title's own image is in
 * that table; the core's anonymous mapping is not. This stage puts the *caller* of a
 * libkernel function inside an anonymous page, which is what a call made from core code
 * looks like to that walk: the page tails into pthread_mutex_lock with its return
 * address inside the page. */
struct Call
{
    void *function;
    void *argument;
};
std::mutex anon_caller_mutex;

void run_anonymous_caller_stage()
{
    record.stage = "anon-caller";
    record.requested = 1;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    write_record();

    void *page = mmap(nullptr, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED)
    {
        record.stage = "anon-caller-mmap-failed";
        write_record();
        return;
    }
    /* mov rax,[rdi] ; mov rdi,[rdi+8] ; call rax ; xor eax,eax ; ret */
    const unsigned char code[] = {0x48, 0x8b, 0x07, 0x48, 0x8b, 0x7f,
                                  0x08, 0xff, 0xd0, 0x31, 0xc0, 0xc3};
    std::memcpy(page, code, sizeof(code));
    if (mprotect(page, 0x4000, PROT_READ | PROT_EXEC) != 0)
    {
        record.stage = "anon-caller-mprotect-failed";
        write_record();
        munmap(page, 0x4000);
        return;
    }

    static Call call{};
    call.function = reinterpret_cast<void *>(&pthread_mutex_lock_wrapper);
    call.argument = &anon_caller_mutex;

    pthread_t thread;
    const int created =
        pthread_create(&thread, nullptr, reinterpret_cast<void *(*)(void *)>(page), &call);
    record.created = created == 0 ? 1 : 2;
    write_record();
    if (created == 0)
    {
        pthread_join(thread, nullptr);
        record.joined = 1;
        record.ran = 1;
        write_record();
    }
    munmap(page, 0x4000);
}

/* Confirmation for the crash chain the console named: a new thread's thread-specific
 * data path (pthread_getspecific -> pthread_get_specificarray_np) resolves a caller
 * address to a module and dereferences the miss. The caller here is the anonymous
 * page, which is what a call made from core code looks like. */
extern "C" void *pthread_getspecific_wrapper(void *key)
{
    return pthread_getspecific(static_cast<pthread_key_t>(reinterpret_cast<uintptr_t>(key)));
}

void run_anonymous_tsd_stage()
{
    record.stage = "anon-tsd";
    record.requested = 1;
    record.created = 0;
    record.ran = 0;
    record.joined = 0;
    bodies_ran.store(0);
    write_record();

    pthread_key_t key;
    if (pthread_key_create(&key, nullptr) != 0)
    {
        record.stage = "anon-tsd-key-failed";
        write_record();
        return;
    }
    void *page = mmap(nullptr, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED)
    {
        record.stage = "anon-tsd-mmap-failed";
        write_record();
        return;
    }
    /* mov rax,[rdi] ; mov rdi,[rdi+8] ; call rax ; xor eax,eax ; ret */
    const unsigned char code[] = {0x48, 0x8b, 0x07, 0x48, 0x8b, 0x7f,
                                  0x08, 0xff, 0xd0, 0x31, 0xc0, 0xc3};
    std::memcpy(page, code, sizeof(code));
    if (mprotect(page, 0x4000, PROT_READ | PROT_EXEC) != 0)
    {
        record.stage = "anon-tsd-mprotect-failed";
        write_record();
        munmap(page, 0x4000);
        return;
    }

    static Call call{};
    call.function = reinterpret_cast<void *>(&pthread_getspecific_wrapper);
    call.argument = reinterpret_cast<void *>(static_cast<uintptr_t>(key));

    pthread_t thread;
    const int created =
        pthread_create(&thread, nullptr, reinterpret_cast<void *(*)(void *)>(page), &call);
    record.created = created == 0 ? 1 : 2;
    write_record();
    if (created == 0)
    {
        pthread_join(thread, nullptr);
        record.joined = 1;
        record.ran = 1;
        write_record();
    }
    munmap(page, 0x4000);
}

extern "C" void ps5_thread_test_if_requested()
{
    FILE *control = std::fopen("/app0/thread-test.txt", "r");
    if (!control)
        return;
    unsigned requested = 0;
    const int fields = std::fscanf(control, "%u", &requested);
    std::fclose(control);
    std::remove("/app0/thread-test.txt");
    if (fields != 1 || requested == 0 || requested > 64)
        requested = 32;

    record.hardware = std::thread::hardware_concurrency();
    std::fprintf(stderr, "thread test: requested=%u hardware_concurrency=%u\n", requested,
                 record.hardware);

    /* Small counts first, so a crash at N leaves the largest N that worked. */
    run_stage("single", 1);
    run_stage("pair", 2);
    for (unsigned count = 4; count <= requested; count *= 2)
    {
        char stage[32];
        std::snprintf(stage, sizeof(stage), "pool-%u", count);
        run_stage(stage, count);
    }

    /* Then the two operations PPSSPP's workers do before they do anything else. */
    run_slot_stage("mutex-1", 1, mutex_worker);
    run_slot_stage("mutex-32", 32, mutex_worker);
    run_slot_stage("condvar-1", 1, condition_worker);
    run_slot_stage("condvar-32", 32, condition_worker);
    run_anonymous_entry_stage();
    run_core_thread_stage();
    run_core_entry_stage();
    run_anonymous_caller_stage();
    run_anonymous_tsd_stage();

    record.stage = "done";
    write_record();
    std::fprintf(stderr, "thread test: passed\n");
}
