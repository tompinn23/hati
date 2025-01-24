#include "dl.h"
#include "log.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdbool.h>

hati_dl *hati_dl_new(const char *url) {
    hati_dl *dl = calloc(1, sizeof(*dl));
    if(!dl) {
        return NULL;
    }
    dl->url = strdup(url);
    if(!dl->url) {
        free(dl);
        return NULL;
    }

    return dl;
}

size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    int fd = (int)((uintptr_t)userdata);
    ssize_t ret = 0;
    size_t realsize = size * nmemb;
    for(;;) {
        if((ret = write(fd, ptr, realsize)) < 0) {
            if(errno == EINTR) {
                continue;
            }
            return CURL_WRITEFUNC_ERROR;
        }
        return ret;
    }
    return CURL_WRITEFUNC_ERROR;
}

static int curl_progress_cb(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    hati_dl *dl = (hati_dl *)user;
    off_t current_sz, total_sz;
    if(dl->response_code >= 300) {
        return 0;
    }

    if(dlnow < 0 || dltotal <= 0 || dlnow > dltotal) {
        return 0;
    }

    current_sz = dl->initial_sz + dlnow;
    
    total_sz = dl->initial_sz + dltotal;
    if(dl->prev_progress == total_sz) {
        return 0;
    }

    /* call progress */
    hati_dl_progress_data progress = {0};
    progress.key = dl->key;
    progress.total = dltotal;
    progress.downloaded = dlnow;
    dl->callback(dl->dlctx, dl->filename, HATI_DL_PROGRESS, &progress);

    dl->prev_progress = current_sz;
    return 0;    
}

static size_t curl_parseheader_cb(void *ptr, size_t size, size_t nmemb, void *user) {
    hati_dl *dl = (hati_dl *)user;
    size_t realsize = size * nmemb;
    long response_code;
    (void)ptr;

    curl_easy_getinfo(dl->curl, CURLINFO_RESPONSE_CODE, &response_code);
    if(dl->response_code != response_code) {
        dl->response_code = response_code;
    }

    return realsize;
}

static int curl_add_dl(CURLM *multi, hati_dl *dl, const char *download_dir) {
    struct stat st;
    int ret = 0;
    int flags = O_WRONLY | O_CREAT;

    dl->curl = curl_easy_init();


    const char *useragent = getenv("HTTP_USER_AGENT");

    curl_easy_reset(dl->curl);
    curl_easy_setopt(dl->curl, CURLOPT_URL, dl->url);
    curl_easy_setopt(dl->curl, CURLOPT_ERRORBUFFER, dl->error_buffer);
    curl_easy_setopt(dl->curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(dl->curl, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(dl->curl, CURLOPT_FILETIME, 1L);
	curl_easy_setopt(dl->curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(dl->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(dl->curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
    curl_easy_setopt(dl->curl, CURLOPT_XFERINFODATA, (void*)dl);
    curl_easy_setopt(dl->curl, CURLOPT_HEADERFUNCTION, curl_parseheader_cb);
	curl_easy_setopt(dl->curl, CURLOPT_HEADERDATA, (void *)dl);
	curl_easy_setopt(dl->curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
	curl_easy_setopt(dl->curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(dl->curl, CURLOPT_TCP_KEEPIDLE, 60L);
	curl_easy_setopt(dl->curl, CURLOPT_TCP_KEEPINTVL, 60L);
	curl_easy_setopt(dl->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(dl->curl, CURLOPT_PRIVATE, (void *)dl);

    if(useragent != NULL) {
        curl_easy_setopt(dl->curl, CURLOPT_USERAGENT, useragent);
    }

    if(dl->mtime_existing) {
        curl_easy_setopt(dl->curl, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
        curl_easy_setopt(dl->curl, CURLOPT_TIMEVALUE, dl->mtime_existing);
    } else if(fstatat(dl->dirfd, dl->tmpfilename, &st, 0) == 0 && dl->allow_resume) {
        curl_easy_setopt(dl->curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)st.st_size);
        dl->initial_sz = st.st_size;
        /* remove create flag and set file to append */
        flags = (flags & ~O_CREAT) | O_APPEND;
    }

    dl->tmpfd = openat(dl->dirfd, dl->tmpfilename, flags, 0);
    if(dl->tmpfd < 0) {
        un_log_errno(LOG_ERR, "failed to open temporary file %s/%s", download_dir, dl->tmpfilename);
        ret = -1;
        goto err;
    }
    curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, (void*)((uintptr_t)dl->tmpfd));
    curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_multi_add_handle(multi, dl->curl);

    if((dl->key = dl->callback(dl->dlctx, dl->filename, HATI_DL_START, NULL)) < 0) {
        un_log(LOG_ERR, "PROGRESS callback indicated error!");
        curl_multi_remove_handle(multi, dl->curl);
        ret = -1;
        goto err;
    }
    
    return 0;
err:
    curl_easy_cleanup(dl->curl);
    dl->curl = NULL;
    close(dl->tmpfd);
    dl->tmpfd = -1;
    return ret;
}

static int curl_check_finished(CURLM *multi, CURLMsg *msg, int *active_dl) {
    hati_dl *dl;
    CURL *curl = msg->easy_handle;
    CURLcode curlerr;
    char *effective_url;

    curlerr = curl_easy_getinfo(curl, CURLINFO_PRIVATE, &dl);
    if(curlerr != CURLE_OK) {
        return -1;
    }

    curlerr = msg->data.result;

    switch(curlerr) {
        case CURLE_OK:
            if(dl->response_code >= 400) {
                
            }
    }
}

int hati_dl_operate(struct hati_list *downloads, int max_dl, const char *download_dir) {
    int active_dl = 0;
    int dirfd = -1;
    char tmpbuf[4096];
    int ret;

    size_t dlsize = hati_list_length(downloads);
    CURLM *multi = curl_multi_init();

    dirfd = open(download_dir, O_PATH);

    struct hati_list *cur = downloads;
    while(active_dl > 0 || cur) {
        CURLMcode mc;
        while(active_dl < max_dl && cur) {
            hati_dl *dl = container_of(cur, dl, list);
            if(curl_add_dl(multi, dl, download_dir) == 0) {
                cur = cur->next;
            } else {
                cur = NULL;
                ret = -1;
                un_log(LOG_ERR, "failed to initialize a download for %s\n", dl->url);
            }
            active_dl++;
        }

        mc = curl_multi_perform(multi, &active_dl);
        if(mc == CURLM_OK) {
            mc = curl_multi_wait(multi, NULL, 0, 1000, NULL);
        }

        if(mc != CURLM_OK) {
            un_log(LOG_ERR, "curl returned error %d from transfer", mc);
            cur = NULL;
            ret = -1;
        }

        while(true) {
            int msgs_left = 0;
            CURLMsg *msg = curl_multi_info_read(multi, &msgs_left);
            if(!msg) {
                break;
            }

            if(msg->msg == CURLMSG_DONE) {
                int rc = curl_check_finished(multi, msg, &active_dl);
                if(rc == -1) {
                    cur = NULL;
                    ret = -1;
                }
            } else {
                un_log(LOG_ERR, "curl transfer error %d", msg->msg);
            }
        }
    }
}
