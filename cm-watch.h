/*
 * COREMU Parallel Emulator Framework
 *
 * Copyright (C) 2010 Parallel Processing Institute, Fudan Univ.
 *  <http://ppi.fudan.edu.cn/system_research_group>
 *
 * Authors:
 *  Zhaoguo Wang    <zgwang@fudan.edu.cn>
 *  Yufei Chen      <chenyufei@fudan.edu.cn>
 *  Ran Liu         <naruilone@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _CM_WATCH_H
#define _CM_WATCH_H

#include <stdio.h>
#include <stdint.h>
#include "cpu.h"
#include "queue.h"
#include "cm-intr.h"

typedef long CMWatchID;
typedef struct CMWatchAddrRange {
    ram_addr_t   ram_addr_offset;
    target_ulong vaddr;
    target_ulong len;
} CMWatchAddrRange;

typedef void (*CMIntr_Trigger)(void *opaque);
typedef struct CMWatchEntry {
    CMWatchID cm_wid;
    CMWatchAddrRange cm_wrange;
    CMIntr_Trigger cm_wtrigger;
    uint8_t cm_invalidate_flag;
} CMWatchEntry;

typedef struct CMWatchPage {
    queue_t *cm_watch_q;
    target_ulong cnt;
} CMWatchPage;


typedef struct CMWatchReq {
    CMIntr *base;
    CMWatchAddrRange cm_wrange;
    int flag;                   // 1: watch 0 unwatch
} CMWatchReq;

enum {
    WATCH_START = 0,
    WATCH_STOP,
    WATCH_INSERT,
    WATCH_REMOVE,
};

void cm_watch_init(ram_addr_t ram_offset, ram_addr_t size);
void cm_insert_watch_point(CMWatchID id, target_ulong start, target_ulong len);
void cm_remove_watch_point(CMWatchID id, target_ulong start, target_ulong len);
bool cm_is_watch_addr_p(ram_addr_t addr);
int cm_get_watch_index(void);

#endif
