/*
** server.h -- The internal interface for hibus server.
**
** Copyright (c) 2020 FMSoft (http://www.fmsoft.cn)
**
** Author: Vincent Wei (https://github.com/VincentWei)
**
** This file is part of hiBus.
**
** hiBus is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** hiBus is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see http://www.gnu.org/licenses/.
*/

#ifndef _HIBUS_SERVER_H_
#define _HIBUS_SERVER_H_

#include <hibox/gslist.h>
#include <hibox/avl.h>
#include <hibox/kvlist.h>
#include <hibox/safe_list.h>

#include "hibus.h"

#define MAX_CLIENT_FD   1024

struct WSClient_;
struct USClient_;

/* Endpoint types */
enum {
    ET_BUILTIN = 0,
    ET_UNIX_SOCKET,
    ET_WEB_SOCKET,
};

/* Endpoint status */
enum {
    ES_AUTHING = 0,     // authenticating
    ES_CLOSING,         // force to close the endpoint due to the failed authentication,
                        // RPC timeout, or ping-pong timeout.
    ES_IDLE,            // the endpoint is idle.
    ES_BUSY,            // the endpoint is busy for a call to procedure.
};

/* A hiBus Client */
typedef struct BusEndpoint_
{
    struct avl_node node;

    int     endpoint_type;
    int     endpoint_status;

    union {
        struct WSClient_ *wsc;
        struct USClient_ *usc;
    };

    char*   host_name;
    char*   app_name;
    char*   runner_name;

    /* All methods registered by this endpoint */
    struct kvlist method_list;

    /* All bubbles registered by this endpoint */
    struct kvlist bubble_list;

    /* All pending calls sent to this endpoint */
    struct safe_list pending_calling;

    /* the challenge code for authentication */
    const char* challenge_code;
} BusEndpoint;

struct WSServer_;
struct USServer_;

/* The hiBus Server */
typedef struct BusServer_
{
    struct WSServer_ *ws_srv;
    struct USServer_ *us_srv;

    unsigned int nr_endpoints;

    /* All endpoints indexed by socket file descriptor.
       The builtin endpoint always occupied the first slot (with index = 0).
       We can quickly find one endpoint with the file descriptor by
       accessing this array. */
    BusEndpoint* endpoints [MAX_CLIENT_FD + 1];

    /* The AVL tree using endpoint as the key, and BusClient* as the value */
    struct avl_tree endpoint_tree;
} BusServer;

/* Config Options */
typedef struct ServerConfig_
{
    /* Config Options */
    const char *host;
    const char *origin;
    const char *unixsocket;
    const char *port;
    const char *sslcert;
    const char *sslkey;
    int accesslog;
    int max_frm_size;
    int backlog;
    int websocket;
    int use_ssl;
} ServerConfig;

#endif /* !_HIBUS_SERVER_H_*/

