/* Indirect include of header.h.
   This creates a transitive inclusion path so that main.c can include
   header.h through two different routes, testing #pragma once deduplication. */
#include "header.h"
