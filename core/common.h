//
// Created by user on 6/29/25.
//
// Common header for non-UI files - includes kernwin.hpp

#ifndef COMMON_H
#define COMMON_H

// Include base common header (IDA SDK without kernwin, std lib, json)
#include "common_base.h"
#include "sdk/common.h"  // For LogLevel enum
using claude::LogLevel;  // so we dont have to prefix it with claude::

// Only include kernwin.hpp if we haven't already included Qt headers
// This allows UI files to include this after ui_v2_common.h
#ifndef QT_VERSION
    #include <kernwin.hpp>
#endif

// Re-define macros after potential kernwin.hpp include
#define fopen dont_use_fopen
#define fclose dont_use_fclose
#define fread dont_use_fread
#define fwrite dont_use_fwrite
#define fgetc dont_use_fgetc
#define snprintf dont_use_snprintf

#endif //COMMON_H
