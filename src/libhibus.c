/*
** libhibus.c -- The code for hiBus client.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/un.h>
#include <sys/time.h>

#include <hibox/utils.h>
#include <hibox/ulog.h>
#include <hibox/md5.h>
#include <hibox/json.h>

#include "hibus.h"

struct _hibus_conn {
    char* srv_host_name;
    char* own_host_name;
    char* app_name;
    char* runner_name;

    int fd;
};

/* return NULL for error */
static char* read_text_payload_from_us (int fd, int* len)
{
    ssize_t n = 0;
    USFrameHeader header;
    char *payload = NULL;

    n = read (fd, &header, sizeof (USFrameHeader));
    if (n > 0) {
        if (header.type == US_OPCODE_TEXT &&
                header.payload_len > 0) {
            payload = malloc (header.payload_len + 1);
        }
        else {
            ULOG_WARN ("Bad payload type (%d) and length (%d)\n",
                    header.type, header.payload_len);
            return NULL;  /* must not the challenge code */
        }
    }

    if (payload == NULL) {
        ULOG_ERR ("Failed to allocate memory for payload.\n");
        return NULL;
    }
    else {
        n = read (fd, payload, header.payload_len);
        if (n != header.payload_len) {
            ULOG_ERR ("Failed to read payload.\n");
            goto failed;
        }

        payload [header.payload_len] = 0;
        if (len)
            *len = header.payload_len;
    }

    ULOG_INFO ("Got payload: \n%s\n", payload);

    return payload;

failed:
    free (payload);
    return NULL;
}

/* return zero for success */
static int check_the_first_packet (int fd)
{
    char* payload;
    int len;
    hibus_json *jo = NULL, *jo_tmp;

    payload = read_text_payload_from_us (fd, &len);
    if (payload == NULL) {
        goto failed;
    }

    jo = json_object_from_string (payload, len, 2);
    if (jo == NULL) {
        goto failed;
    }

    free (payload);
    payload = NULL;

    if (json_object_object_get_ex (jo, "packageType", &jo_tmp)) {
        const char *pack_type;
        pack_type = json_object_get_string (jo_tmp);
        ULOG_INFO ("packageType: %s\n", pack_type);

        if (strcasecmp (pack_type, "error") == 0) {
            ULOG_WARN ("Refued by server\n");
            goto failed;
        }
        else if (strcasecmp (pack_type, "auth") == 0) {
            if (json_object_object_get_ex (jo, "challengeCode", &jo_tmp)) {
                const char *ch_code;
                ch_code = json_object_get_string (jo_tmp);
                ULOG_INFO ("challengeCode: %s\n", ch_code);
            }
        }
    }
    else {
        ULOG_WARN ("No packageType field\n");
    }

    json_object_put (jo);
    return 0;

failed:
    if (jo)
        json_object_put (jo);
    if (payload)
        free (payload);

    return -1;
}

#define CLI_PATH    "/var/tmp/"
#define CLI_PERM    S_IRWXU

/* returns fd if all OK, -1 on error */
int hibus_connect_via_unix_socket (const char* path_to_socket,
        const char* app_name, const char* runner_name, hibus_conn** conn)
{
    int fd, len;
    struct sockaddr_un unix_addr;
    char peer_name [33];

    /* create a Unix domain stream socket */
    if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
        ULOG_ERR ("Failed to call `socket` in hibus_connect_via_unix_socket: %s\n",
                strerror (errno));
        return (-1);
    }

    {
        md5_ctx_t ctx;
        unsigned char md5_digest[16];

        md5_begin (&ctx);
        md5_hash (app_name, strlen (app_name), &ctx);
        md5_hash ("/", 1, &ctx);
        md5_hash (runner_name, strlen (runner_name), &ctx);
        md5_end (md5_digest, &ctx);
        bin2hex (md5_digest, 16, peer_name);
    }

    /* fill socket address structure w/our address */
    memset (&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    /* On Linux sun_path is 108 bytes in size */
    sprintf (unix_addr.sun_path, "%s%s-%05d", CLI_PATH, peer_name, getpid());
    len = sizeof (unix_addr.sun_family) + strlen (unix_addr.sun_path);

    ULOG_INFO ("The client addres: %s\n", unix_addr.sun_path);

    unlink (unix_addr.sun_path);        /* in case it already exists */
    if (bind (fd, (struct sockaddr *) &unix_addr, len) < 0) {
        ULOG_ERR ("Failed to call `bind` in hibus_connect_via_unix_socket: %s\n",
                strerror (errno));
        goto error;
    }
    if (chmod (unix_addr.sun_path, CLI_PERM) < 0) {
        ULOG_ERR ("Failed to call `chmod` in hibus_connect_via_unix_socket: %s\n",
                strerror (errno));
        goto error;
    }

    /* fill socket address structure w/server's addr */
    memset (&unix_addr, 0, sizeof(unix_addr));
    unix_addr.sun_family = AF_UNIX;
    strcpy (unix_addr.sun_path, path_to_socket);
    len = sizeof (unix_addr.sun_family) + strlen (unix_addr.sun_path);

    if (connect (fd, (struct sockaddr *) &unix_addr, len) < 0) {
        ULOG_ERR ("Failed to call `connect` in hibus_connect_via_unix_socket: %s\n",
                strerror (errno));
        goto error;
    }

    /* try to read challenge code */
    if (check_the_first_packet (fd))
        goto error;

    return (fd);

error:
    close (fd);
    return (-1);
}

int hibus_connect_via_web_socket (const char* host_name, int port,
        const char* app_name, const char* runner_name, hibus_conn** conn)
{
    return -HIBUS_SC_NOT_IMPLEMENTED;
}

int hibus_disconnect (hibus_conn* conn)
{
    assert (conn);

    free (conn->srv_host_name);
    free (conn->own_host_name);
    free (conn->app_name);
    free (conn->runner_name);
    close (conn->fd);
    free (conn);

    return HIBUS_SC_OK;
}

const char* hibus_conn_srv_host_name (hibus_conn* conn)
{
    return conn->srv_host_name;
}

const char* hibus_conn_own_host_name (hibus_conn* conn)
{
    return conn->own_host_name;
}

const char* hibus_conn_app_name (hibus_conn* conn)
{
    return conn->app_name;
}

const char* hibus_conn_runner_name (hibus_conn* conn)
{
    return conn->runner_name;
}

int hibus_conn_socket_fd (hibus_conn* conn)
{
    return conn->fd;
}

