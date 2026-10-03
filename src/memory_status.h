/* Shared-pool pressure and reports for the frontend and hardware cores. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    int ps5_memory_pressure(size_t request);
    void ps5_memory_report(const char *event, size_t request, int result);
#ifdef __cplusplus
}
#endif
