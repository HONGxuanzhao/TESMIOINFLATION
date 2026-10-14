// Small header-only binding used by TESMIOINFLATION.cpp.
#ifndef TESMIO_PLUGIN_H
#define TESMIO_PLUGIN_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tesmio_api.h"

static const TsmHost* H = NULL;
static HMODULE g_exe = NULL;
static BYTE* g_exeBase = NULL;
static SIZE_T g_exeSize = 0;

static void TsmBind(const TsmHost* host)
{
    H = host;
    g_exe = (HMODULE)host->exeModule;
    g_exeBase = host->exeBase;
    g_exeSize = host->exeSize;
}

static bool HostHasField(size_t offset, size_t length)
{
    return H && H->structSize >= offset &&
           H->structSize - offset >= length;
}

#define Logf (H->log)

#endif // TESMIO_PLUGIN_H
