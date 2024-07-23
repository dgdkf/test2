
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <stdlib.h>
#include <ngx_md5.h>
#include <math.h>

#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/msg.h>
#include<stdio.h>
#include<string.h>
#include<sys/stat.h>
#include<unistd.h>
#include<errno.h>


// #include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>

// #define MYPORT  8887
// #define BUFFER_SIZE 1024

typedef struct {
        long int my_msg_type; //64λ������������Ϊlong
        char msg[OUT_STR_MAX_LEN];
}msg_buf_old;

// ��ʼ��body��������ͷ�ڵ㣬��֤����ĩβ���ǿ�ָ��
// ��ʱͷ�ڵ���ngx_http_write_filter
static ngx_int_t ngx_http_write_filter_init(ngx_conf_t *cf);



// ��������ֻ��һ��init��������ʼ������ָ��
static ngx_http_module_t  ngx_http_write_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_write_filter_init,            /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_write_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_write_filter_module_ctx,     /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


ngx_int_t
ngx_http_write_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    off_t                      size, sent, nsent, limit;
    ngx_uint_t                 last, flush, sync;
    ngx_msec_t                 delay;
    ngx_chain_t               *cl, *ln, **ll, *chain;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_request_t        *sr;
    ngx_buf_t                 *buf=NULL;

    c = r->connection;

    if (c->error) {
        return NGX_ERROR;
    }

    if(r->main->mirror_num == 1){
        if(r->main->aga_chain){
            r->out = r->main->aga_chain;
        }
    }

    size = 0;
    flush = 0;
    sync = 0;
    last = 0;
    ll = &r->out;

    /* find the size, the flush point and the last link of the saved chain */

    for (cl = r->out; cl; cl = cl->next) {
        ll = &cl->next;

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "write old buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);

        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "zero size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        if (ngx_buf_size(cl->buf) < 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "negative size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        size += ngx_buf_size(cl->buf);

        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }

        if (cl->buf->sync) {
            sync = 1;
        }

        if (cl->buf->last_buf) {
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "old---last ==== 1---:");
            last = 1;
        }
    }

    /* add the new chain to the existent one */

    for (ln = in; ln; ln = ln->next) {
        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = ln->buf;
        *ll = cl;
        ll = &cl->next;

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "write new buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);

        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "zero size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        if (ngx_buf_size(cl->buf) < 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "negative size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        size += ngx_buf_size(cl->buf);

        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }

        if (cl->buf->sync) {
            sync = 1;
        }

        if (cl->buf->last_buf) {
            last = 1;
        }
    }

    *ll = NULL;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    /*
     * avoid the output if there are no last buf, no flush point,
     * there are the incoming bufs and the size of all bufs
     * is smaller than "postpone_output" directive
     */

    if(last)
    {
        // size_t blacklist[10]={0};
        // size_t i,count=0;
        // for ( i = 0; i < (size_t)r->main->mirror_num; i++)
        // {
        //     if(r->main->blist[i] > 0 && r->main->wlist[i] == 0)
        //     {
        //         blacklist[count] = i;
        //         count ++;
        //     }
        // }

        // if(count > 0)
        // {
        //     send_alert_execute_v11(r,blacklist,count,BODY_ALERT);
        // }
    }

    if (c->write->delayed) {
        c->buffered |= NGX_HTTP_WRITE_BUFFERED;
        return NGX_AGAIN;
    }

    if (size == 0
        && !(c->buffered & NGX_LOWLEVEL_BUFFERED)
        && !(last && c->need_last_buf)&& !(c->buffered & NGX_SSL_BUFFERED))
    {
        if (last || flush || sync) {
            for (cl = r->out; cl; /* void */) {
                ln = cl;
                cl = cl->next;
                ngx_free_chain(r->pool, ln);
            }

            r->out = NULL;
            c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;

            if(r->main->ready_tailer){

                r->main->tailer_sent = 1;
            }

            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "the http output chain is empty");

        ngx_debug_point();

        return NGX_ERROR;
    }

    if (r->limit_rate) {
        if (r->limit_rate_after == 0) {
            r->limit_rate_after = clcf->limit_rate_after;
        }

        limit = (off_t) r->limit_rate * (ngx_time() - r->start_sec + 1)
                - (c->sent - r->limit_rate_after);

        if (limit <= 0) {
            c->write->delayed = 1;
            delay = (ngx_msec_t) (- limit * 1000 / r->limit_rate + 1);
            ngx_add_timer(c->write, delay);

            c->buffered |= NGX_HTTP_WRITE_BUFFERED;

            return NGX_AGAIN;
        }

        if (clcf->sendfile_max_chunk
            && (off_t) clcf->sendfile_max_chunk < limit)
        {
            limit = clcf->sendfile_max_chunk;
        }

    } else {
        limit = clcf->sendfile_max_chunk;
    }

    sent = c->sent;

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"-send_chain-");

    chain = c->send_chain(c, r->out, limit);


    if (chain == NGX_CHAIN_ERROR) {
        c->error = 1;
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"-write_filter-ngx_error");
        return NGX_ERROR;
    }

    if (r->limit_rate) {

        nsent = c->sent;

        if (r->limit_rate_after) {

            sent -= r->limit_rate_after;
            if (sent < 0) {
                sent = 0;
            }

            nsent -= r->limit_rate_after;
            if (nsent < 0) {
                nsent = 0;
            }
        }

        delay = (ngx_msec_t) ((nsent - sent) * 1000 / r->limit_rate);

        if (delay > 0) {
            limit = 0;
            c->write->delayed = 1;
            ngx_add_timer(c->write, delay);
        }
    }

    if (limit
        && c->write->ready
        && c->sent - sent >= limit - (off_t) (2 * ngx_pagesize))
    {
        c->write->delayed = 1;
        ngx_add_timer(c->write, 1);
    }

    for (cl = r->out; cl && cl != chain; /* void */) {
        ln = cl;
        cl = cl->next;
        ngx_free_chain(r->pool, ln);
    }

    if(r->main->mirror_num == 1){
        r->out = chain;

        if (chain) {
            ll = &r->main->aga_chain;

            for ( ln = chain ; ln ; ln = ln->next)  {
                cl = ngx_alloc_chain_link(r->pool);
                if(cl == NULL) {
                    return NGX_ERROR;
                }

                if(ngx_buf_size(ln->buf)>0){
                    buf = ngx_create_temp_buf(r->pool,r->upstream->conf->buffer_size);
                    if(buf ==  NULL){
                        return NGX_ERROR;
                    }

                    cl->buf = buf;
                    cl->next = NULL;
                    cl->buf->last = ngx_cpymem(cl->buf->last, ln->buf->pos, ngx_buf_size(ln->buf));
                    
                }else{
                    buf = ngx_calloc_buf(r->pool);
                    if(buf ==   NULL){
                        return NGX_ERROR;
                    }

                    cl->buf = buf;
                    cl->next = NULL;
                    cl->buf->last_buf = 1; 
                }

                *ll = cl;
                ll = &cl->next;
            }

            r->out  = NULL;

            c->buffered |= NGX_HTTP_WRITE_BUFFERED;

            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"mirror_num1 again");
            return NGX_AGAIN;

        }else{
            r->main->aga_chain = NULL;
            c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;

            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"mirror_num1 again null");

            return NGX_OK;
        }

    }else{
        r->out=NULL;
        r->main->aga_chain = chain;
    }

    for(sr = r->main;sr;sr=sr->next) {
        if(sr->done==0){
            c->data = r;
            break;
        }
    }
    

    if (chain) {
        c->buffered |= NGX_HTTP_WRITE_BUFFERED;
        r->aft_eagain = 1;
        r->main->send_again = 1;
        
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"r->key=%d send_again=%d,tailer_sent=%d",
                                            r->key,r->main->send_again,r->main->tailer_sent);
        return NGX_AGAIN;

    }else{
        r->main->send_again = 0;
        r->aft_eagain = 0;
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"writer_filter send_again=%d",r->main->send_again);
    }

    /*-----------------------------------------------------------------*/
    if(r->upstream){

        if(r->main->tailer_sent == 0){
            
            if(r->main->ready_tailer && r->main->send_again==0){
                c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;
                r->main->tailer_sent = 1;

            } else {
                c->buffered |= NGX_HTTP_WRITE_BUFFERED;
            }
            
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, 
                                                "writer_filter_tailer_sent=0 c->buffered=%d",c->buffered);
            return NGX_OK;
        }
   }

   
    
    c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;
    
    /*-----------------------------------------------------------------*/

    if ((c->buffered & NGX_LOWLEVEL_BUFFERED) && r->postponed == NULL) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"-write_filter-ngx_AGAIN");
        return NGX_AGAIN;
    }

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"-write_filter-ngx_OK");
    return NGX_OK;
}


static ngx_int_t
ngx_http_write_filter_init(ngx_conf_t *cf)
{
    ngx_http_top_body_filter = ngx_http_write_filter;

    return NGX_OK;
}
