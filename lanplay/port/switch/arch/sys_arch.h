/* lwIP OS abstraction types for Horizon, implemented with ams::os in sys_arch.cpp.
 * SPDX-License-Identifier: GPL-2.0-only */
#pragma once

typedef struct { void *p; } sys_sem_t;
typedef struct { void *p; } sys_mutex_t;
typedef struct { void *p; } sys_mbox_t;
typedef void *sys_thread_t;
typedef int   sys_prot_t;

#define sys_sem_valid(s)            ((s) != NULL && (s)->p != NULL)
#define sys_sem_set_invalid(s)      do { if ((s) != NULL) (s)->p = NULL; } while (0)
#define sys_mutex_valid(m)          ((m) != NULL && (m)->p != NULL)
#define sys_mutex_set_invalid(m)    do { if ((m) != NULL) (m)->p = NULL; } while (0)
#define sys_mbox_valid(b)           ((b) != NULL && (b)->p != NULL)
#define sys_mbox_set_invalid(b)     do { if ((b) != NULL) (b)->p = NULL; } while (0)
#define sys_sem_valid_val(s)        ((s).p != NULL)
#define sys_sem_set_invalid_val(s)  do { (s).p = NULL; } while (0)
#define sys_mbox_valid_val(b)       ((b).p != NULL)
#define sys_mbox_set_invalid_val(b) do { (b).p = NULL; } while (0)
