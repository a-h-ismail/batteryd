/*
Copyright (C) 2025 Ahmad Ismail
SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef COMMON_H
#define COMMON_H

enum service_status
{
    SUCCESS = 101,
    VALUE_TOO_SMALL,
    VALUE_TOO_LARGE,
    SYSTEM_FAILURE
};

enum opcode
{
    SET_THRESHOLD,
    GET_THRESHOLD,
    RELOAD_CONFIG
};

#endif