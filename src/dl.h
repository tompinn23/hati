#pragma once

#include "list.h"

#include <stdio.h>

#include "curl/curl.h"
#include "curl/multi.h"

typedef enum {
    HATI_DL_START,
    HATI_DL_PROGRESS,
    HATI_DL_COMPLETE,
} hati_dl_event_type;

typedef int (*hati_dl_callback)(void *, const char *, hati_dl_event_type, void *);


typedef struct hati_dl {
    char *url;
    char *filename;
    char *tmpfilename;

    struct hati_list *list;

    int allow_resume;
    long mtime_existing;
    
    hati_dl_callback callback;
    void *dlctx;
    int key; /* key returned via callback that we pass back to help callback identify me */

    /* initialized when we actually start the download */
    CURL *curl;
    char error_buffer[CURL_ERROR_SIZE];
    long response_code;
    off_t initial_sz;
    off_t prev_progress;
    int dirfd;
    int tmpfd;
} hati_dl;

typedef struct hati_dl_progress_data {
    int key;
    off_t total;
    off_t downloaded;
} hati_dl_progress_data;

typedef struct hati_dl_complete_data {
    int key;
} hati_dl_complete_data;

hati_dl *hati_dl_new(const char *url);

int hati_dl_operate(struct hati_list *downloads, int max_dl, const char *download_dir);