
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_BUF_H_INCLUDED_
#define _NGX_BUF_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef void *            ngx_buf_tag_t;

typedef struct ngx_buf_s  ngx_buf_t;

struct ngx_buf_s {
    u_char          *pos;
    u_char          *last;
    off_t            file_pos;
    off_t            file_last;

    u_char          *start;         /* start of buffer */
    u_char          *end;           /* end of buffer */
    ngx_buf_tag_t    tag;
    ngx_file_t      *file;
    ngx_buf_t       *shadow;


    /* the buf's content could be changed */
    unsigned         temporary:1;

    /*
     * the buf's content is in a memory cache or in a read only memory
     * and must not be changed
     */
    unsigned         memory:1;

    /* the buf's content is mmap()ed and must not be changed */
    unsigned         mmap:1;

    unsigned         recycled:1;
    unsigned         in_file:1;
    unsigned         flush:1;
    unsigned         sync:1;
    unsigned         last_buf:1;
    unsigned         last_in_chain:1;

    unsigned         last_shadow:1;
    unsigned         temp_file:1;

    /* STUB */ int   num;
};


struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};


//zdc11

struct  ngx_flow_node_s
{
    ngx_int_t                     key;
    ngx_str_t                     *name;//当前node对应的上游执行体信息

    ngx_chain_t                   *header_data;//header 数据要不要独立出来？
    ngx_uint_t                     status_code;
    ngx_int_t                      content_len_n;
    
    ngx_uint_t                     content_tp_hash;
    size_t                         h_score;
    ngx_uint_t                     method;
    
    unsigned                       chunked:1;
    unsigned                       e_body:1;
    unsigned                       vo_alert:1;
    unsigned                       alert_sent:1;
    unsigned                       header_data_ready:1;
    unsigned                       b_arb_ready:1; //包体内容已完成裁决前准备
    unsigned                       last_flag:1;
    unsigned                       keepalive:1;
    unsigned                       c_destroyed:1;

    ngx_chain_t                   *body_data_chain; //存储所有数据的
    uint32_t                       bd_crc; //一次裁决的包体crc的值
    uint32_t                       bd_crc_all;
    ngx_int_t                      b_score;
    size_t                         size; //当前node包含的字节数
    size_t                         sum;

    
    // unsigned                      e_header:1; //header是否已经输出            
    ngx_flow_node_t                *next;
    ngx_str_t                      location_value;

    ngx_chain_t                    *free;
    ngx_chain_t                    *free_bufs;
    size_t                         free_size;

};

// struct  ngx_extor_info_s
// {
//     ngx_str_t            *name;
//     ngx_chain_t          *header_data;//header ����Ҫ��Ҫ����������
//     ngx_uint_t            status_code;
//     off_t                 content_length_n;
//     ngx_str_t             content_type;
//     unsigned              body_data_ready:1;
//     unsigned              last_flag:1;
//     ngx_extor_info_t     *next;
// };



// struct ngx_cookie_list_s{
//     ngx_int_t rm_lable;
//     ngx_int_t list_key;
//     // ngx_http_request_t *rm;
//     ngx_str_t curi[UP_NUM_MAX];
//     ngx_str_t setcookie[UP_NUM_MAX];
// };

// typedef struct cookie_list  cookie_l;
// struct ngx_cookie_store_s{
//     ngx_cookie_list_t   *cookie_list;
//     ngx_cookie_store_t  *next;
// };

// // �ͷ������ڵ㣬���ڿ���������
// #define ngx_free_cookie_store(pool, cl)
//     cl->next = pool->cookie_store;
//     pool->cookie_store = cl


//���������Ĳ����ṹ
typedef struct {
    ngx_int_t    num;
    size_t       size;
} ngx_bufs_t;


typedef struct ngx_output_chain_ctx_s  ngx_output_chain_ctx_t;

typedef ngx_int_t (*ngx_output_chain_filter_pt)(void *ctx, ngx_chain_t *in);

typedef void (*ngx_output_chain_aio_pt)(ngx_output_chain_ctx_t *ctx,
    ngx_file_t *file);

struct ngx_output_chain_ctx_s {
    ngx_buf_t                   *buf;
    ngx_chain_t                 *in;
    ngx_chain_t                 *free;
    ngx_chain_t                 *busy;

    unsigned                     sendfile:1;
    unsigned                     directio:1;
    unsigned                     unaligned:1;
    unsigned                     need_in_memory:1;
    unsigned                     need_in_temp:1;
    unsigned                     aio:1;

#if (NGX_HAVE_FILE_AIO || NGX_COMPAT)
    ngx_output_chain_aio_pt      aio_handler;
#if (NGX_HAVE_AIO_SENDFILE || NGX_COMPAT)
    ssize_t                    (*aio_preload)(ngx_buf_t *file);
#endif
#endif

#if (NGX_THREADS || NGX_COMPAT)
    ngx_int_t                  (*thread_handler)(ngx_thread_task_t *task,
                                                 ngx_file_t *file);
    ngx_thread_task_t           *thread_task;
#endif

    off_t                        alignment;

    ngx_pool_t                  *pool;
    ngx_int_t                    allocated;
    ngx_bufs_t                   bufs;
    ngx_buf_tag_t                tag;

    ngx_output_chain_filter_pt   output_filter;
    void                        *filter_ctx;
};


typedef struct {
    ngx_chain_t                 *out;
    ngx_chain_t                **last;
    ngx_connection_t            *connection;
    ngx_pool_t                  *pool;
    off_t                        limit;
} ngx_chain_writer_ctx_t;


#define NGX_CHAIN_ERROR     (ngx_chain_t *) NGX_ERROR


#define ngx_buf_in_memory(b)        (b->temporary || b->memory || b->mmap)
#define ngx_buf_in_memory_only(b)   (ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_special(b)                                                   \
    ((b->flush || b->last_buf || b->sync)                                    \
     && !ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_sync_only(b)                                                 \
    (b->sync                                                                 \
     && !ngx_buf_in_memory(b) && !b->in_file && !b->flush && !b->last_buf)

#define ngx_buf_size(b)                                                      \
    (ngx_buf_in_memory(b) ? (off_t) (b->last - b->pos):                      \
                            (b->file_last - b->file_pos))

ngx_buf_t *ngx_create_temp_buf(ngx_pool_t *pool, size_t size);
ngx_chain_t *ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs);


#define ngx_alloc_buf(pool)  ngx_palloc(pool, sizeof(ngx_buf_t))
#define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))

ngx_chain_t *ngx_alloc_chain_link(ngx_pool_t *pool);
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl



ngx_int_t ngx_output_chain(ngx_output_chain_ctx_t *ctx, ngx_chain_t *in);
ngx_int_t ngx_chain_writer(void *ctx, ngx_chain_t *in);

ngx_int_t ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain,
    ngx_chain_t *in);
ngx_chain_t *ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free);
void ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free,
    ngx_chain_t **busy, ngx_chain_t **out, ngx_buf_tag_t tag);

off_t ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit);

ngx_chain_t *ngx_chain_update_sent(ngx_chain_t *in, off_t sent);

#endif /* _NGX_BUF_H_INCLUDED_ */
