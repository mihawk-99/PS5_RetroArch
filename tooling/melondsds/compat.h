/* Thread naming is optional and unsupported by this native title runtime. */
#pragma once
#include <pthread.h>
#define pthread_set_name_np(thread, name) ((void)(thread), (void)(name))
