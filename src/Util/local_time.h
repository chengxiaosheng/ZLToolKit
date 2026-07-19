//
// Created by alex on 2022/5/29.
//

#ifndef UTIL_LOCALTIME_H
#define UTIL_LOCALTIME_H
#include <time.h>
#include "toolkit/exports.h"

namespace toolkit {
ZLTOOLKIT_EXPORT void no_locks_localtime(struct tm *tmp, time_t t);
ZLTOOLKIT_EXPORT void local_time_init();
ZLTOOLKIT_EXPORT int get_daylight_active();

} // namespace toolkit
#endif // UTIL_LOCALTIME_H
