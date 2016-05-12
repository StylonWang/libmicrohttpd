/*
 * This file is part of libmicrohttpd (C) 2007 Christian Grothoff (and other
 * contributing authors)
 * 
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file fileserver_example.c
 * @brief example for how to use libmicrohttpd to serve files (with directory support)
 * @author Christian Grothoff
 */

#include "platform.h"
#include <dirent.h>
#include <microhttpd.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include "debug.h"

#define PAGE "<html><head><title>File not found</title></head><body>File not found</body></html>"

struct path_context {
    FILE           *file;
    DIR            *dir;
    char            parent_path[256];
    char            parent_url[256];
};

static char     g_cwd[256];

struct path_context *
new_path_context(FILE * f, DIR * d, const char *path, const char *url)
{
    struct path_context *c = malloc(sizeof(*c));
    if (NULL == c)
        return NULL;

    c->file = f;
    c->dir = d;
    snprintf(c->parent_path, sizeof(c->parent_path), "%s", path);
    snprintf(c->parent_url, sizeof(c->parent_url), "%s", url);

    return c;
}

static          ssize_t
file_reader(void *cls, uint64_t pos, char *buf, size_t max)
{
    struct path_context *c = cls;

    (void)fseek(c->file, pos, SEEK_SET);
    return fread(buf, 1, max, c->file);
}

static void
file_free_callback(void *cls)
{
    struct path_context *c = cls;
    fclose(c->file);
    free(c);
}

static void
dir_free_callback(void *cls)
{
    DIR            *dir = cls;
    struct path_context *c = cls;
    if (c->dir != NULL)
        closedir(c->dir);
    free(c);
}

static          ssize_t
dir_reader(void *cls, uint64_t pos, char *buf, size_t max)
{
    struct path_context *c = cls;
    struct dirent  *e;

    dbgprint("+ url=%s, dir=%s\n", c->parent_url, c->parent_path);

    if (max < 512) {
        dbgprint("-\n");
        return 0;
    }
    do {
        e = readdir(c->dir);
        if (e == NULL) {
            dbgprint("-\n");
            return MHD_CONTENT_READER_END_OF_STREAM;
        }
    } while (e->d_name[0] == '.');
    //skip "." and ".." folders

        dbgprint("- %s\n", e->d_name);
    return snprintf(buf, max,
                    "<a href=\"%s%s%s\">%s</a><br>\n",
                    c->parent_url, 
                    // if parent url ends in '/', no need to insert another '/'
                    c->parent_url[strlen(c->parent_url)-1]=='/'? "" : "/",
                    e->d_name,
                    e->d_name);
}


static int
ahc_echo(void *cls,
         struct MHD_Connection *connection,
         const char *url,
         const char *method,
         const char *version,
         const char *upload_data,
         size_t * upload_data_size, void **ptr)
{
    static int      aptr;
    struct MHD_Response *response;
    int             ret;
    FILE           *file;
    DIR            *dir;
    struct stat     buf;
    char            emsg[1024];
    char            path[256];
    struct path_context *c;

    if (strcmp("/", url))
        //not root URL
    {
        snprintf(path, sizeof(path), "%s%s", g_cwd, url);
    } else {
        //root URL
        snprintf(path, sizeof(path), "%s", g_cwd);
    }

    dbgprint("+ url='%s', path='%s'\n", url, path);

//TODO:check if path falls outside current working folder, for security `reasons

    if (0 != strcmp(method, MHD_HTTP_METHOD_GET)) {
                dbgprint("-\n");
                return MHD_NO;  /* unexpected method */
    }
    if (&aptr != *ptr) {
        /* do never respond on first call */
        *ptr = &aptr;
        dbgprint("-\n");
        return MHD_YES;
    }
    *ptr = NULL;                /* reset when done */

    if(stat(path, &buf)!=0) {
            dbgprint("'%s' error: %s\n", path, strerror(errno));
            response = MHD_create_response_from_buffer(strlen(PAGE),
                                                       (void *)PAGE,
                                                    MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
            return ret;
    }

    if (S_ISREG(buf.st_mode)) { // a regular file
        file = fopen(path, "rb");
        if (NULL == file) {
            dbgprint("'%s' fopen failure: %s\n", path, strerror(errno));
            response = MHD_create_response_from_buffer(strlen(PAGE),
                                                       (void *)PAGE,
                                                    MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
            return ret;
        }

        dbgprint("invoke file_reader\n");
        c = new_path_context(file, NULL, path, url);
        response = MHD_create_response_from_callback(buf.st_size, 32 * 1024,    /* 32k page size */
                                                     &file_reader,
                                                     //file,
                                                     c,
                                                     &file_free_callback);
        if (response == NULL) {
            fclose(file);
            dbgprint("-\n");
            return MHD_NO;
        }
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    } 
    else if (S_ISDIR(buf.st_mode)) { // is a folder
        dbgprint("opendir %s\n", path);
        dir = opendir(path);
        if (dir == NULL) {
            /*
             * most likely cause: more concurrent requests than  available
             * file descriptors / 2
             */
            snprintf(emsg,
                     sizeof(emsg),
                     "Failed to open directory `%s': %s\n",
                     &url[1],
                     strerror(errno));
            response = MHD_create_response_from_buffer(strlen(emsg),
                                                       emsg,
                                                     MHD_RESPMEM_MUST_COPY);
            if (response == NULL) {
                dbgprint("- failed to craft error response: opendir failed\n");
                return MHD_NO;
            }
            ret = MHD_queue_response(connection, MHD_HTTP_SERVICE_UNAVAILABLE, response);
            MHD_destroy_response(response);
        } else {
            dbgprint("invoke dir_reader\n");
            c = new_path_context(NULL, dir, path, url);
            response = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN,
                                                         32 * 1024,
                                                         &dir_reader,
                                                         //dir,
                                                         c,
                                                         &dir_free_callback);
            if (response == NULL) {
                closedir(dir);
                dbgprint("-\n");
                return MHD_NO;
            }
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
        }
    }
    else {

            dbgprint("'%s' not a regular file or folder n", path);
            response = MHD_create_response_from_buffer(strlen(PAGE),
                                                       (void *)PAGE,
                                                    MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
    }

    dbgprint("-\n");
    return ret;
}


static int      g_quit = 0;
void 
signal_handler(int signo)
{
    printf("got signal %d, quit!\n", signo);
    g_quit = 1;
}

int
main(int argc, char *const *argv)
{
    struct MHD_Daemon *d;

    if (argc != 2) {
        printf("%s PORT\n", argv[0]);
        return 1;
    }
    if (NULL == getcwd(g_cwd, sizeof(g_cwd))) {
        printf("unable to get current work directory, abort.\n");
        return 1;
    }
    signal(SIGINT, signal_handler);

    d = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG,
                         atoi(argv[1]),
                         NULL, NULL, &ahc_echo, PAGE, MHD_OPTION_END);
    if (d == NULL)
        return 1;
    while (!g_quit) {
        sleep(1);
    }
    MHD_stop_daemon(d);
    printf("terminating...\n");
    return 0;
}
