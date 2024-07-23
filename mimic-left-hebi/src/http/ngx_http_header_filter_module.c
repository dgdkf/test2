// annotated by chrono since 2016
//
// * ngx_http_header_filter

/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>
// #include <ngx_http_config.h>

// ngx_cookie_store_t *cookie_store;
// ngx_http_request_t *curr_r_main=NULL;

// ngx_http_header_filter_module是modules数组里第一个header filter模块
// 因为nginx依据数组顺序设置链表指针
// 所以它是header过滤链表里的最后一个节点
// 作用是整理headers_out里的头信息，拼接成响应头字符串
// 最后交给ngx_http_write_filter输出，即发送到socket

// 初始化header过滤链表头节点，保证链表末尾不是空指针
// 此时头节点是ngx_http_header_filter
static ngx_int_t ngx_http_header_filter_init(ngx_conf_t *cf);

// 作用是整理headers_out里的头信息，拼接成响应头字符串
// 最后交给ngx_http_write_filter输出，即发送到socket
static ngx_int_t ngx_http_header_filter(ngx_http_request_t *r);

// ngx_int_t storage_cookie(ngx_http_request_t *r, ngx_str_t setcookie);


// 函数表里只有一个init函数，初始化链表指针
static ngx_http_module_t  ngx_http_header_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_header_filter_init,           /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


// 没有其他配置相关的信息
ngx_module_t  ngx_http_header_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_header_filter_module_ctx,    /* module context */
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


// 输出响应头里的server信息，短字符串，不含版本号
// server_tokens off使用此字符串
// static u_char ngx_http_server_string[] = "Server: nginx" CRLF;//伪装Server指纹
static u_char ngx_http_server_string[] = "Server: mimic" CRLF;

// 输出响应头里的server信息，长字符串，含版本号
// server_tokens on使用此字符串
static u_char ngx_http_server_full_string[] = "Server: " NGINX_VER CRLF;

// 1.11.10新增的build参数
static u_char ngx_http_server_build_string[] = "Server: " NGINX_VER_BUILD CRLF;


// 状态行字符串的关联数组
// 采用状态码减取基准的方法实现映射，很巧妙
// 例如201-200=1, 302-300 + 7
// 可以在这里加入自己的状态码定义
static ngx_str_t ngx_http_status_lines[] = {

    ngx_string("200 OK"),
    ngx_string("201 Created"),
    ngx_string("202 Accepted"),
    ngx_null_string,  /* "203 Non-Authoritative Information" */
    ngx_string("204 No Content"),
    ngx_null_string,  /* "205 Reset Content" */
    ngx_string("206 Partial Content"),

    /* ngx_null_string, */  /* "207 Multi-Status" */

#define NGX_HTTP_LAST_2XX  207

// 使用此偏移量计算3xx的位置
#define NGX_HTTP_OFF_3XX   (NGX_HTTP_LAST_2XX - 200)

    /* ngx_null_string, */  /* "300 Multiple Choices" */

    ngx_string("301 Moved Permanently"),
    ngx_string("302 Moved Temporarily"),
    ngx_string("303 See Other"),
    ngx_string("304 Not Modified"),
    ngx_null_string,  /* "305 Use Proxy" */
    ngx_null_string,  /* "306 unused" */
    ngx_string("307 Temporary Redirect"),
    ngx_string("308 Permanent Redirect"),

// 使用此偏移量计算4xx的位置
#define NGX_HTTP_LAST_3XX  309

#define NGX_HTTP_OFF_4XX   (NGX_HTTP_LAST_3XX - 301 + NGX_HTTP_OFF_3XX)

    ngx_string("400 Bad Request"),
    ngx_string("401 Unauthorized"),
    ngx_string("402 Payment Required"),
    ngx_string("403 Forbidden"),
    ngx_string("404 Not Found"),
    ngx_string("405 Not Allowed"),
    ngx_string("406 Not Acceptable"),
    ngx_null_string,  /* "407 Proxy Authentication Required" */
    ngx_string("408 Request Time-out"),
    ngx_string("409 Conflict"),
    ngx_string("410 Gone"),
    ngx_string("411 Length Required"),
    ngx_string("412 Precondition Failed"),
    ngx_string("413 Request Entity Too Large"),
    ngx_string("414 Request-URI Too Large"),
    ngx_string("415 Unsupported Media Type"),
    ngx_string("416 Requested Range Not Satisfiable"),
    ngx_null_string,  /* "417 Expectation Failed" */
    ngx_null_string,  /* "418 unused" */
    ngx_null_string,  /* "419 unused" */
    ngx_null_string,  /* "420 unused" */
    ngx_string("421 Misdirected Request"),
    ngx_null_string,  /* "422 Unprocessable Entity" */
    ngx_null_string,  /* "423 Locked" */
    ngx_null_string,  /* "424 Failed Dependency" */
    ngx_null_string,  /* "425 unused" */
    ngx_null_string,  /* "426 Upgrade Required" */
    ngx_null_string,  /* "427 unused" */
    ngx_null_string,  /* "428 Precondition Required" */
    ngx_string("429 Too Many Requests"),

// 使用此偏移量计算5xx的位置
// 1.12.0之前是422
#define NGX_HTTP_LAST_4XX  430

#define NGX_HTTP_OFF_5XX   (NGX_HTTP_LAST_4XX - 400 + NGX_HTTP_OFF_4XX)

    ngx_string("500 Internal Server Error"),
    ngx_string("501 Not Implemented"),
    ngx_string("502 Bad Gateway"),
    ngx_string("503 Service Temporarily Unavailable"),
    ngx_string("504 Gateway Time-out"),
    ngx_string("505 HTTP Version Not Supported"),
    ngx_null_string,        /* "506 Variant Also Negotiates" */
    ngx_string("507 Insufficient Storage"),

    /* ngx_null_string, */  /* "508 unused" */
    /* ngx_null_string, */  /* "509 unused" */
    /* ngx_null_string, */  /* "510 Not Extended" */

#define NGX_HTTP_LAST_5XX  508

};


// 常用头与headers_out里成员的映射关系
// 使用了宏offsetof，直接得到成员的地址
// 本模块并不使用，供其他模块使用
ngx_http_header_out_t  ngx_http_headers_out[] = {
    { ngx_string("Server"), offsetof(ngx_http_headers_out_t, server) },
    { ngx_string("Date"), offsetof(ngx_http_headers_out_t, date) },
    { ngx_string("Content-Length"),
                 offsetof(ngx_http_headers_out_t, content_length) },
    { ngx_string("Content-Encoding"),
                 offsetof(ngx_http_headers_out_t, content_encoding) },
    { ngx_string("Location"), offsetof(ngx_http_headers_out_t, location) },
    { ngx_string("Last-Modified"),
                 offsetof(ngx_http_headers_out_t, last_modified) },
    { ngx_string("Accept-Ranges"),
                 offsetof(ngx_http_headers_out_t, accept_ranges) },
    { ngx_string("Expires"), offsetof(ngx_http_headers_out_t, expires) },
    { ngx_string("Cache-Control"),
                 offsetof(ngx_http_headers_out_t, cache_control) },
    { ngx_string("ETag"), offsetof(ngx_http_headers_out_t, etag) },

    { ngx_null_string, 0 }
};


// 作用是整理headers_out里的头信息，拼接成响应头字符串
// 首先检查r->header_sent，如果已经调用此函数（即已经发送了）则直接返回
// 计算状态行+响应头的长度
// 没有自定义状态行，那就需要根据状态码映射到标准描述信息
// 遍历响应头链表，添加自定义头，注意不检查是否有常用头（如server等）
// 所以常用头不应该放进链表，而应该使用指针直接赋值
// 最后交给ngx_http_write_filter输出，即发送到socket
static ngx_int_t
ngx_http_header_filter(ngx_http_request_t *r)
{
    u_char                    *p;
    size_t                     len;
    ngx_str_t                  host, *status_line;
    ngx_buf_t                 *b;
    ngx_uint_t                 status, i, port;
    ngx_chain_t                out;
    ngx_list_part_t           *part;
    ngx_table_elt_t           *header;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;
    u_char                     addr[NGX_SOCKADDR_STRLEN];


    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"-----------header_filter=%d",r->header_sent);
    // 首先检查r->header_sent，如果已经调用此函数（即已经发送了）则直接返回
    if (r->header_sent) {
        return NGX_OK;
    }

    // 设置r->header_sent，防止重复发送
    r->header_sent = 1;

    // 子请求不会发送头
    // 也就是说不会走socket，可以加入父请求的数据链里发送
    // if (r != r->main) {
    //     return NGX_OK;
    // }

    // http 协议不是1.0/1.1也不发送
    if (r->http_version < NGX_HTTP_VERSION_10) {
        return NGX_OK;
    }

    // 查看客户端发送请求的方法，如果是head请求
    // 表示不要求body，置header_only标志
    if (r->method == NGX_HTTP_HEAD) {
        // r->header_only = 1; //zdc 20201200
    }

    // 响应头里有last_modified_time
    // 但状态码不是正常，那么就清除此头
    if (r->headers_out.last_modified_time != -1) {
        if (r->headers_out.status != NGX_HTTP_OK
            && r->headers_out.status != NGX_HTTP_PARTIAL_CONTENT
            && r->headers_out.status != NGX_HTTP_NOT_MODIFIED)
        {
            r->headers_out.last_modified_time = -1;
            r->headers_out.last_modified = NULL;
        }
    }

    // 下面开始计算状态行+响应头的长度

    // 首先是状态行
    len = sizeof("HTTP/1.x ") - 1 + sizeof(CRLF) - 1
          /* the end of the header */
          + sizeof(CRLF) - 1;

    /* status line */

    // 如果我们在headers_out设置了自己的状态信息
    // 那么就比较简单，直接使用即可
    if (r->headers_out.status_line.len) {

        // 长度加上headers_out.status_line的长度
        len += r->headers_out.status_line.len;

        // 状态行直接使用请求结构体里的字符串
        status_line = &r->headers_out.status_line;
#if (NGX_SUPPRESS_WARN)
        status = 0;
#endif

    } else {

        // 没有自定义状态行，那就需要根据状态码映射到标准描述信息

        // 获取状态码
        status = r->headers_out.status;

        // 2xx代码
        // 之后的3xx/4xx的逻辑基本相同
        if (status >= NGX_HTTP_OK
            && status < NGX_HTTP_LAST_2XX)
        {
            /* 2XX */

            if (status == NGX_HTTP_NO_CONTENT) {
                // r->header_only = 1; //zdc20201202
                ngx_str_null(&r->headers_out.content_type);
                r->headers_out.last_modified_time = -1;
                r->headers_out.last_modified = NULL;
                r->headers_out.content_length = NULL;
                r->headers_out.content_length_n = -1;
            }

            // 减去200，从&ngx_http_status_lines里得到状态码描述信息
            status -= NGX_HTTP_OK;
            status_line = &ngx_http_status_lines[status];
            len += ngx_http_status_lines[status].len;

        } else if (status >= NGX_HTTP_MOVED_PERMANENTLY
                   && status < NGX_HTTP_LAST_3XX)
        {
            /* 3XX */

            if (status == NGX_HTTP_NOT_MODIFIED) {
                // r->header_only = 1; //zdc20201202
            }

            status = status - NGX_HTTP_MOVED_PERMANENTLY + NGX_HTTP_OFF_3XX;
            status_line = &ngx_http_status_lines[status];
            len += ngx_http_status_lines[status].len;

        } else if (status >= NGX_HTTP_BAD_REQUEST
                   && status < NGX_HTTP_LAST_4XX)
        {
            /* 4XX */
            status = status - NGX_HTTP_BAD_REQUEST
                            + NGX_HTTP_OFF_4XX;

            status_line = &ngx_http_status_lines[status];
            len += ngx_http_status_lines[status].len;

        } else if (status >= NGX_HTTP_INTERNAL_SERVER_ERROR
                   && status < NGX_HTTP_LAST_5XX)
        {
            /* 5XX */
            status = status - NGX_HTTP_INTERNAL_SERVER_ERROR
                            + NGX_HTTP_OFF_5XX;

            status_line = &ngx_http_status_lines[status];
            len += ngx_http_status_lines[status].len;

        } else {
            // 不是标准http状态码
            len += NGX_INT_T_LEN + 1 /* SP */;
            status_line = NULL;
        }

        if (status_line && status_line->len == 0) {
            status = r->headers_out.status;
            len += NGX_INT_T_LEN + 1 /* SP */;
            status_line = NULL;
        }
    }

    // 下面开始计算各种常用头

    // 本location的配置
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    // 是否是完整的server信息
    if (r->headers_out.server == NULL) {
        if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_ON) {
            len += sizeof(ngx_http_server_full_string) - 1;

        } else if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_BUILD) {
            len += sizeof(ngx_http_server_build_string) - 1;

        } else {
            len += sizeof(ngx_http_server_string) - 1;
        }
    }

    // 日期
    if (r->headers_out.date == NULL) {
        len += sizeof("Date: Mon, 28 Sep 1970 06:00:00 GMT" CRLF) - 1;
    }

    // content_type
    if (r->headers_out.content_type.len) {
        len += sizeof("Content-Type: ") - 1
               + r->headers_out.content_type.len + 2;

        if (r->headers_out.content_type_len == r->headers_out.content_type.len
            && r->headers_out.charset.len)
        {
            len += sizeof("; charset=") - 1 + r->headers_out.charset.len;
        }
    }

    // 内容长度
    if (r->headers_out.content_length == NULL
        && r->headers_out.content_length_n >= 0)
    {
        len += sizeof("Content-Length: ") - 1 + NGX_OFF_T_LEN + 2;
    }

    // last_modified
    if (r->headers_out.last_modified == NULL
        && r->headers_out.last_modified_time != -1)
    {
        len += sizeof("Last-Modified: Mon, 28 Sep 1970 06:00:00 GMT" CRLF) - 1;
    }

    c = r->connection;

    // location
    if (r->headers_out.location
        && r->headers_out.location->value.len
        && r->headers_out.location->value.data[0] == '/'
        && clcf->absolute_redirect)
    {
        r->headers_out.location->hash = 0;

        if (clcf->server_name_in_redirect) {
            cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
            host = cscf->server_name;

        } else if (r->headers_in.server.len) {
            host = r->headers_in.server;

        } else {
            host.len = NGX_SOCKADDR_STRLEN;
            host.data = addr;

            if (ngx_connection_local_sockaddr(c, &host, 0) != NGX_OK) {
                return NGX_ERROR;
            }
        }

        port = ngx_inet_get_port(c->local_sockaddr);

        len += sizeof("Location: https://") - 1
               + host.len
               + r->headers_out.location->value.len + 2;

        if (clcf->port_in_redirect) {

#if (NGX_HTTP_SSL)
            if (c->ssl)
                port = (port == 443) ? 0 : port;
            else
#endif
                port = (port == 80) ? 0 : port;

        } else {
            port = 0;
        }

        if (port) {
            len += sizeof(":65535") - 1;
        }

    } else {
        ngx_str_null(&host);
        port = 0;
    }

    // 是否是chunked编码

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, 
                                               "r=%d, r->chunk=%d",r->key,r->chunked);
    if (r->chunked) {
        len += sizeof("Transfer-Encoding: chunked" CRLF) - 1;
    }

    // keepalive
    if (r->headers_out.status == NGX_HTTP_SWITCHING_PROTOCOLS) {
        len += sizeof("Connection: upgrade" CRLF) - 1;

    } else if (r->main->keepalive) {//zdc
//    } else if (r->keepalive) {
        len += sizeof("Connection: keep-alive" CRLF) - 1;

        /*
         * MSIE and Opera ignore the "Keep-Alive: timeout=<N>" header.
         * MSIE keeps the connection alive for about 60-65 seconds.
         * Opera keeps the connection alive very long.
         * Mozilla keeps the connection alive for N plus about 1-10 seconds.
         * Konqueror keeps the connection alive for about N seconds.
         */

        if (clcf->keepalive_header) {
            len += sizeof("Keep-Alive: timeout=") - 1 + NGX_TIME_T_LEN + 2;
        }

    } else {
        len += sizeof("Connection: close" CRLF) - 1;
    }

    // gzip
#if (NGX_HTTP_GZIP)
    if (r->gzip_vary) {
        if (clcf->gzip_vary) {
            len += sizeof("Vary: Accept-Encoding" CRLF) - 1;

        } else {
            r->gzip_vary = 0;
        }
    }
#endif

    // 常用头处理完毕，下面是自定义头

    // 遍历响应头链表
    part = &r->headers_out.headers.part;
    header = part->elts;

    // 注意，这里不检查是否有常用头（如server等）
    // 所以常用头不应该放进链表，而应该使用指针直接赋值
    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        // hash==0就忽略，所以可以用这种方法“删除”头
        if (header[i].hash == 0) {
            continue;
        }

        len += header[i].key.len + sizeof(": ") - 1 + header[i].value.len
               + sizeof(CRLF) - 1;
    }

    // 所有状态行和响应头的长度都已经计算完
    // 可以分配内存了

    // len长度可以容纳所有的响应头信息
    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }

    /* "HTTP/1.x " */
    b->last = ngx_cpymem(b->last, "HTTP/1.1 ", sizeof("HTTP/1.x ") - 1);

    /* status line */

    // 有状态行就拷贝数据
    // 注意用ngx_copy会返回拷贝后的末尾位置信息，方便继续拷贝
    if (status_line) {
        b->last = ngx_copy(b->last, status_line->data, status_line->len);

    } else {
        // 没有状态行信息，只能把数字打印出来
//        b->last = ngx_sprintf(b->last, "%03ui ", status);
    }

    // 状态行结束，加上\r\n
    *b->last++ = CR; *b->last++ = LF;

    // server
    if (r->headers_out.server == NULL) {
        if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_ON) {
            p = ngx_http_server_full_string;
            len = sizeof(ngx_http_server_full_string) - 1;

        } else if (clcf->server_tokens == NGX_HTTP_SERVER_TOKENS_BUILD) {
            p = ngx_http_server_build_string;
            len = sizeof(ngx_http_server_build_string) - 1;

        } else {
            p = ngx_http_server_string;
            len = sizeof(ngx_http_server_string) - 1;
        }

        b->last = ngx_cpymem(b->last, p, len);
    }

    // date
    if (r->headers_out.date == NULL) {
        b->last = ngx_cpymem(b->last, "Date: ", sizeof("Date: ") - 1);
        b->last = ngx_cpymem(b->last, ngx_cached_http_time.data,
                             ngx_cached_http_time.len);

        *b->last++ = CR; *b->last++ = LF;
    }

    // content_type
    if (r->headers_out.content_type.len) {
        b->last = ngx_cpymem(b->last, "Content-Type: ",
                             sizeof("Content-Type: ") - 1);
        p = b->last;
        b->last = ngx_copy(b->last, r->headers_out.content_type.data,
                           r->headers_out.content_type.len);

        if (r->headers_out.content_type_len == r->headers_out.content_type.len
            && r->headers_out.charset.len)
        {
            b->last = ngx_cpymem(b->last, "; charset=",
                                 sizeof("; charset=") - 1);
            b->last = ngx_copy(b->last, r->headers_out.charset.data,
                               r->headers_out.charset.len);

            /* update r->headers_out.content_type for possible logging */

            r->headers_out.content_type.len = b->last - p;
            r->headers_out.content_type.data = p;
        }

        *b->last++ = CR; *b->last++ = LF;
    }

    // content_length
    // 通常我们不需要写字符串，让nginx在这里打印为字符串
    if (r->headers_out.content_length == NULL
        && r->headers_out.content_length_n >= 0)
    {
        b->last = ngx_sprintf(b->last, "Content-Length: %O" CRLF,
                              r->headers_out.content_length_n);
    }

    // last_modified
    if (r->headers_out.last_modified == NULL
        && r->headers_out.last_modified_time != -1)
    {
        b->last = ngx_cpymem(b->last, "Last-Modified: ",
                             sizeof("Last-Modified: ") - 1);
        b->last = ngx_http_time(b->last, r->headers_out.last_modified_time);

        *b->last++ = CR; *b->last++ = LF;
    }

    // location
    if (host.data) {

        p = b->last + sizeof("Location: ") - 1;

        b->last = ngx_cpymem(b->last, "Location: http",
                             sizeof("Location: http") - 1);

#if (NGX_HTTP_SSL)
        if (c->ssl) {
            *b->last++ ='s';
        }
#endif

        *b->last++ = ':'; *b->last++ = '/'; *b->last++ = '/';
        b->last = ngx_copy(b->last, host.data, host.len);

        if (port) {
            b->last = ngx_sprintf(b->last, ":%ui", port);
        }

        b->last = ngx_copy(b->last, r->headers_out.location->value.data,
                           r->headers_out.location->value.len);

        /* update r->headers_out.location->value for possible logging */

        r->headers_out.location->value.len = b->last - p;
        r->headers_out.location->value.data = p;
        ngx_str_set(&r->headers_out.location->key, "Location");

        *b->last++ = CR; *b->last++ = LF;
    }

    // chunked
    if (r->chunked) {
        b->last = ngx_cpymem(b->last, "Transfer-Encoding: chunked" CRLF,
                             sizeof("Transfer-Encoding: chunked" CRLF) - 1);
    }

    // keepalive
    if (r->headers_out.status == NGX_HTTP_SWITCHING_PROTOCOLS) {//main
        b->last = ngx_cpymem(b->last, "Connection: upgrade" CRLF,
                             sizeof("Connection: upgrade" CRLF) - 1);

    } else if (r->main->keepalive) { //zdc
//    } else if (r->keepalive) {
        b->last = ngx_cpymem(b->last, "Connection: keep-alive" CRLF,
                             sizeof("Connection: keep-alive" CRLF) - 1);

        if (clcf->keepalive_header) {
            b->last = ngx_sprintf(b->last, "Keep-Alive: timeout=%T" CRLF,
                                  clcf->keepalive_header);
        }

    } else {
        b->last = ngx_cpymem(b->last, "Connection: close" CRLF,
                             sizeof("Connection: close" CRLF) - 1);
    }

    // gzip
#if (NGX_HTTP_GZIP)
    if (r->gzip_vary) {
        b->last = ngx_cpymem(b->last, "Vary: Accept-Encoding" CRLF,
                             sizeof("Vary: Accept-Encoding" CRLF) - 1);
    }
#endif

    // 自定义头逐个添加
    // 不检查是否与server等常用头冲突
    part = &r->headers_out.headers.part;
    header = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        // hash==0就忽略，所以可以用这种方法“删除”头
        if (header[i].hash == 0) {
            continue;
        }

        b->last = ngx_copy(b->last, header[i].key.data, header[i].key.len);
    
        // 这里在冒号后有一个空格
        *b->last++ = ':'; *b->last++ = ' ';

        b->last = ngx_copy(b->last, header[i].value.data, header[i].value.len);
        *b->last++ = CR; *b->last++ = LF;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "--header_filter--%*s", (size_t) (b->last - b->pos), b->pos);

    /* the end of HTTP header */
    // 响应头结束，再加上一个\r\n
    *b->last++ = CR; *b->last++ = LF;


    //添加返回content-me选项
    size_t  content_me_bl = 0;

    if(r->headers_out.content_me != NULL)
    {
        if(ngx_strcmp(r->headers_out.content_me->value.data,r->content_me) != 0)
        {
            content_me_bl = r->key;
            ngx_log_debug(NGX_LOG_NOTICE, c->log, 0, "Content-Me_abt_error: %V---%s",
                                                    &(r->headers_out.content_me->value),r->content_me);
            send_alert_execute_v11(r,&content_me_bl,1,HEADER_C_ALERT);
        }
    }


    // 两个指针相减，得到头的长度
    r->header_size = b->last - b->pos;

    // 检查是否只需要返回头
    if (r->header_only) {
        // 标记为最后一块数据，之后就不能再发送给客户端了
        b->last_buf = 1;
    }

    // 添加进数据链表
    out.buf = b;
    out.next = NULL;

    // ngx_http_mirror_loc_conf_t  *mlcf;
    if(r->main->mirror_num == 1 || r->main->flow_node == NULL)
    {
        if(r->headers_out.content_length_n == -1){
            r->main->header_chunk = 1;
        }

        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "abort before distributing!");
        r->main->header_abt_sent = 1;
        return ngx_http_write_filter(r, &out);//1010
    }

    //without mimic
    // if(r->main->mirror_num == 0 )
    // {
    //     return  ngx_http_write_filter(r,&out);
    // }
   
   ngx_flow_node_t  *fl_nd;

    //match flow node and decide whether need to create node
    //to header, maybe do not need match this, for only traverse once in header collecting.
//    fl_nd = NULL;
    fl_nd = r->main->flow_node;
    for (; fl_nd; fl_nd = fl_nd->next)
    {
        if(r->key == fl_nd->key)
        {
            break;
        }
    }


    if(fl_nd ==  NULL)
    {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,"--header--fl_nd==NULL--r->key:%i----",r->key);
        return NGX_ERROR;
    }
    fl_nd->free = NULL;
    fl_nd->free_bufs = NULL;
    fl_nd->free_size = 0;
    fl_nd->bd_crc = 0xffffffff;

    fl_nd->name = ngx_palloc(r->main->pool,sizeof(ngx_str_t));
    if(r->upstream){
        fl_nd->name->data = ngx_palloc(r->main->pool,r->upstream->peer.name->len+1);
        fl_nd->name->len  = r->upstream->peer.name->len;
        ngx_cpystrn(fl_nd->name->data,r->upstream->peer.name->data,r->upstream->peer.name->len+1);
    }

    // store header data.
    // header data can remain until finally free.
    fl_nd->header_data = ngx_alloc_chain_link(r->main->pool);
  
    if(fl_nd->header_data == NULL){
        return NGX_ERROR;
    }
     
    fl_nd->header_data->buf = b;
    fl_nd->header_data->next = NULL;
    fl_nd->status_code = r->headers_out.status;

    u_char      *lo_buf=NULL;
    u_char      *ret_l,*ret_r;
    int         lo_len =0;
    ngx_int_t   o_key;

    if(r->headers_out.location == NULL && (fl_nd->status_code == 301 || fl_nd->status_code == 302)){

        lo_buf = ngx_pcalloc(r->pool,b->last - b->pos);
        if(lo_buf == NULL){
            return NGX_ERROR;
        }

        ngx_memzero(lo_buf,b->last - b->pos);
        ngx_memcpy(lo_buf,b->pos,b->last - b->pos);

        ret_l = (u_char*)strstr((char*)lo_buf,"Location: ");
        if(ret_l){
            ret_r = (u_char*)strstr((char*)ret_l,"\r\n");

            if(ret_r){
                lo_len = (int)(ret_r - ret_l - 10);

                r->headers_out.location  = ngx_palloc(r->main->pool,sizeof(ngx_table_elt_t));

                r->headers_out.location->value.data = ngx_palloc(r->main->pool,lo_len + 1);
                
                ngx_memcpy(r->headers_out.location->value.data, ret_l+10, lo_len);

                r->headers_out.location->value.len = lo_len;
            }
        }
    }

    if(r->headers_out.location != NULL)
    {
        if(fl_nd->location_value.data == NULL){

            fl_nd->location_value.data = ngx_palloc(r->main->pool,r->headers_out.location->value.len+1);
            memset(fl_nd->location_value.data, 0,r->headers_out.location->value.len+1);
        }

        if(fl_nd->location_value.data){
            
            memcpy(fl_nd->location_value.data,r->headers_out.location->value.data,r->headers_out.location->value.len);
            fl_nd->location_value.len  = r->headers_out.location->value.len; 
        }

        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0,"----r-location=:%V, fl_nd->loc=%V len =%d",
                                                        &(r->headers_out.location->value), &(fl_nd->location_value),fl_nd->location_value.len);
    }

    if (r->headers_out.content_length == NULL
        && r->headers_out.content_length_n >= 0)
    {
        fl_nd->content_len_n = r->headers_out.content_length_n;
    }
   
    if(r->chunked == 1)
    {
        fl_nd->chunked = r->chunked;
        fl_nd->content_len_n = -1;
    }

    fl_nd->method = r->method;
    fl_nd->e_body = 1;
    r->main->need_body = 1;
    r->exist_body = 1;


    // if (fl_nd->status_code == 304 
    //     || fl_nd->status_code == 204 
    //     || fl_nd->status_code >399 //错误状态将会在output_filter最前面直接被return, 也不存在包体?????? 
    //     || fl_nd->method == NGX_HTTP_HEAD)
    // {
    //     fl_nd->e_body = 0;
    //     fl_nd->last_flag = 1;
    //     r->exist_body = 0;
    // }

     if ( fl_nd->method == NGX_HTTP_HEAD) {
        fl_nd->e_body = 0;
        fl_nd->last_flag = 1;
        r->exist_body = 0;
    }

    r->header_only = 0;

    fl_nd->header_data_ready = 1;

    //***********************************

    ngx_int_t   co_ready = 0;
    ngx_int_t   h_ready[UP_NUM_MAX] = {0};
    //check if need voting; 
    //TODO for fifo with majority
    for( fl_nd = r->main->flow_node; fl_nd ; fl_nd=fl_nd->next )
    {
        if( fl_nd->header_data_ready == 1){
            co_ready++;
            h_ready[fl_nd->key] = 1;
        }
    }
    if(r->upstream!=NULL){
        ngx_log_debug(NGX_LOG_NOTICE, c->log, 0,
                   "ext: %V---http upstream request line: \"%V\"",r->upstream->peer.name, &r->request_line);
    }

    for (fl_nd = r->main->flow_node; fl_nd; fl_nd = fl_nd->next) {
        if(r->key == fl_nd->key){
            break;
        }
    }

    //TODO: FIFO need to modify this condition
    // if(co_ready < r->main->mirror_num)
   if(co_ready <= r->main->mirror_num/2)  {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"header ready=%d",co_ready);
        return NGX_OK;

    }  else  {
        ngx_chain_t     *h_out;
        ngx_int_t       i,j; 
        ngx_int_t       stat_cd[UP_NUM_MAX] = {0};
        off_t           con_len[UP_NUM_MAX] = {0};
        ngx_int_t       score_stat_cd[UP_NUM_MAX]={0};  
        ngx_int_t       score_con_len[UP_NUM_MAX]={0};
        ngx_int_t       score_loc[UP_NUM_MAX] ={0};

        size_t          selected[UP_NUM_MAX] = {99};
        size_t          black_list[UP_NUM_MAX]={0};
        size_t          record_black[UP_NUM_MAX] = {0};
        ngx_int_t       count_black = 0;
        size_t          s_k = 0; 
 
        size_t          index_304[UP_NUM_MAX] = {99};
        ngx_int_t       count_304 = 0;
        size_t          need_self_o = 0;//需要构造错误页面了。
        // size_t          major500 = 0;
        ngx_int_t       trans_stat[UP_NUM_MAX] = {0};
        size_t          chunk_content_trans[UP_NUM_MAX]={0};
        ngx_str_t       location_value[UP_NUM_MAX];
        

        for( fl_nd = r->main->flow_node;fl_nd;fl_nd=fl_nd->next )
        {
            if(fl_nd->header_data_ready == 1){
                ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,"###header--candidate:%V---Status-code:%ui---Con-Len:%i --- key:%ui",
                                                    fl_nd->name,fl_nd->status_code, fl_nd->content_len_n,fl_nd->key);   

                stat_cd[fl_nd->key] = fl_nd->status_code;
                con_len[fl_nd->key] = fl_nd->content_len_n;

                // if(stat_cd[fl_nd->key] == 200 && con_len[fl_nd->key] == -1)
                if(stat_cd[fl_nd->key] == 200 && con_len[fl_nd->key] == -1)
                {
                    chunk_content_trans[fl_nd->key] = 1;
                }

                if(stat_cd[fl_nd->key]>399 && stat_cd[fl_nd->key]<500)
                {
                    stat_cd[fl_nd->key] = 404;                    
                }

                if(stat_cd[fl_nd->key] == 304)
                {
                    index_304[count_304] = fl_nd->key;
                    count_304++;
                    stat_cd[fl_nd->key] = 200;                              //转换200
                    trans_stat[fl_nd->key] = 1 ;
                }

                if(stat_cd[fl_nd->key]==302 || stat_cd[fl_nd->key]==301)
                {
                    trans_stat[fl_nd->key] = 1 ;
         //           count_302++;
                    location_value[fl_nd->key].data = ngx_palloc(r->main->pool,fl_nd->location_value.len+1);
                    memset(location_value[fl_nd->key].data, 0, fl_nd->location_value.len+1);

                    if(location_value[fl_nd->key].data){

                        location_value[fl_nd->key].len = fl_nd->location_value.len; 

                        ngx_cpystrn(location_value[fl_nd->key].data,fl_nd->location_value.data,fl_nd->location_value.len);
                    } 
                }   
            }            
        }

        //header arbiter
        for ( i = 0; i < r->main->mirror_num; i++)
        {
            if(h_ready[i]==0)  {    continue;   }

            for ( j = 0; j < r->main->mirror_num; j++)
            {
                if(h_ready[j]==0) {     continue;   }

                if(stat_cd[i]==stat_cd[j])  {       score_stat_cd[i]++;     }

               // if(con_len[i] == con_len[j] || (stat_cd[i]== 404 && stat_cd[j] == 404)  //屏蔽掉404长度
                if(con_len[i] == con_len[j] || (stat_cd[i]>399 && stat_cd[i]<500 && stat_cd[j]>399 && stat_cd[j]<500)
                                            || (trans_stat[i] == 1) 
                                            || (trans_stat[j] == 1)                     //屏蔽掉304，302，302的长度比对
                                            || (chunk_content_trans[i]==1)
                                            || (chunk_content_trans[j]==1))             //屏蔽200chunk       
                {
                    score_con_len[i]++;
                }

                if( (stat_cd[i]==302 && stat_cd[j]==302) || (stat_cd[i]==301 && stat_cd[j] == 301) )
                {                        
                    if( location_value[i].len == location_value[j].len )
                    {
                        if(strncmp( (const char*)location_value[i].data, (const char*)location_value[j].data, location_value[i].len) == 0){
                            score_loc[i]++;
                            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"--location_success---%V",&location_value[i],&location_value[j]);
                        }  
                    }             
                }
            }

            if(stat_cd[i] >= 500) {
                ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"--%i---500+ server failed-----",i);

                size_t bl[1];
                bl[0] = (size_t)i;
                send_alert_execute_v11(r,bl,1,HEADER_STAT_ALERT);

                // if(score_stat_cd[i] > r->main->mirror_num/2)  {
                //     major500 = 1;
                // }
            } 
        }

        //statistics
        for ( i = 0; i < r->main->mirror_num; i++) {
            if(h_ready[i] == 0) { 
                continue; 
            }

            //状态码统计得分
            if(score_stat_cd[i] <= r->main->mirror_num/2) {
                ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"--%i---status code unconsistent-----",i);     
                record_black[i]++;
            } 

            //长度统计得分
            if(score_con_len[i] <= r->main->mirror_num/2)  {           
                record_black[i]++;                              //record_black中序号代表fl_nd->key
            }               

            //302，301，location统计得分
            if(stat_cd[i]==302 || stat_cd[i]==301) {
//                if(score_loc[i] <= r->main->mirror_num/2){   record_black[i]++;   } //-----------loc not alert
            }
        }
        

        for ( i = 0,count_black = 0; i < r->main->mirror_num; i++){
            if(h_ready[i]==0)   { 
                continue; 
            }

            if(record_black[i] > 0) {
                black_list[count_black] = i;    //black_list 中变量存放的key
                count_black++;

            } else {
                selected[s_k] = i;
                s_k++;                            
            }
        }
  
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "count_black = %i, s_k=%i",count_black,s_k);
        
        if(s_k > 0)
        {
            o_key= selected[0];    

            if(stat_cd[o_key] == 200)
            {
                if(count_304 > 0)
                {
                    for(i=0;i<r->main->mirror_num;i++)
                    {
                        if(trans_stat[i] == 1 && stat_cd[i] == 200)                      //改动
                        {
                            o_key = i;                    
                            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"---get 304----%i",i); 
                        }
                    }
                }

                if( count_304 == r->main->mirror_num - 1 ){
                    r->main->alert_exempt = 1;
                }
            } 

        } else  {
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"------header arbiter not get majority consistent------");
            if(co_ready < r->main->mirror_num){
                return NGX_OK;
            }

            need_self_o = 1;
            o_key = 0; //tmp 没啥用，避免后面处理逻辑出现问题。
            r->main->need_body = 0;

            if(count_304 > 0){
                ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"--all-status--differ-----304 output--"); 
                o_key = index_304[rand()%(count_304)]; 
                need_self_o = 0;

            } else {
                if(PRESTIGE_ON){
                    r->main->select_en = 1;
                    need_self_o = 0;

                    get_share_prestige(r);

                    for(fl_nd = r->main->flow_node;fl_nd;fl_nd=fl_nd->next){
                        if(fl_nd->key == r->main->select_key) {
                            o_key = r->main->select_key;
                            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"select header key=%d",o_key);
                            break;
                        }
                    }
                }
            }
        } 

        //reporting executor alert detected
        if(count_black > 0 && count_black <= r->main->mirror_num && co_ready == r->main->mirror_num)
        {
            send_alert_execute_v11(r,black_list,count_black,HEADER_STAT_ALERT);
        }

        if(r->main->header_abt_sent == 1){
            return NGX_OK;
        }

        for( fl_nd = r->main->flow_node;fl_nd;fl_nd=fl_nd->next )
        {
            if(fl_nd->key == o_key) break;
        }
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
                                    "######--HeaderABT-Winner:%V---Status-code:%ui--found Valid:%ui",
                                        fl_nd->name,fl_nd->status_code,fl_nd->key);  
        
        if(fl_nd->content_len_n == -1 && fl_nd->header_data_ready == 1)
        {
            r->main->header_chunk = 1;
        }
        h_out = fl_nd->header_data;
        
        //根据选定状态码确定是否需要发送包体。
        
        if( (fl_nd->status_code > 300 )
                                    || fl_nd->status_code == 204 
                                    || fl_nd->method == NGX_HTTP_HEAD  ){
                                    
            r->main->need_body = 0;
            r->main->abt302 = 300;
        }

        //确定需要构造包体的条件判定
        //但是在哪构造呢？头部裁决完直接造？其实也可以，header_only的情况。
        
        if(stat_cd[o_key] >= 400 && stat_cd[o_key] < 500){
            need_self_o = 1;
        }
        
        if(r->main->need_body == 0) {
            ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"header_fiter_set_un_wake");
            r->un_wake = 1;
            h_out->buf->last_buf = 1;     

            if(need_self_o == 1)
            {
                r->main->header_chunk = 0;
                r->main->self_e_page = 1;
                // return NGX_ERROR;
                ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,"###---Headers Unconsistent---Not Found");  
                ngx_chain_t *ch_err,*cb_err;
                ngx_buf_t *h_e,*b_e;
                size_t len_e;
                len_e = sizeof("HTTP/1.1 404 \r\nContent-Type: text/html;charset=UTF-8\r\nContent-Length: 296\r\nDate: Mon, 11 Nov 2019 07:56:56 GMT\r\nConnection: close\r\n\r\n")-1;

                h_e = ngx_create_temp_buf(r->pool,len_e);
                h_e->last = ngx_cpymem(h_e->last,
                            "HTTP/1.1 404 \r\nContent-Type: text/html;charset=UTF-8\nContent-Length: 296\r\nDate: Mon, 11 Nov 2019 07:56:56 GMT\r\nConnection: close\r\n\r\n",len_e);
                ch_err = ngx_alloc_chain_link(r->pool);
                ch_err->buf = h_e;        
        
                len_e = sizeof("<!DOCTYPE html><html><head><title>NOT FOUND</title>\n<style>body {width: 35em;margin: 0 auto;font-family: Tahoma, Verdana, Arial, sans-serif;}</style></head><body><h1>NOT FOUND</h1>\n<p>Sorry, the page you are looking for is currently unavailable.<br/>Please try again later.</p></body></html>\r\n\r\n\n")
                -1;
                // len_e = sizeof("all black happened!")-1;
                b_e = ngx_create_temp_buf(r->pool,len_e);
                b_e->last = ngx_cpymem(b_e->last,"<!DOCTYPE html><html><head><title>NOT FOUND</title>\n<style>body {width: 35em;margin: 0 auto;font-family: Tahoma, Verdana, Arial, sans-serif;}</style></head><body><h1>NOT FOUND</h1>\n<p>Sorry, the page you are looking for is currently unavailable.<br/>Please try again later.</p></body></html>\r\n\r\n\n"
                                ,len_e);
                b_e->last_buf = 1;
                cb_err = ngx_alloc_chain_link(r->pool);
                cb_err->buf = b_e;
                cb_err->next = NULL;
                ch_err->next = cb_err;
                h_out = ch_err; 
            }
        }

        r->main->header_abt_sent = 1;
        return ngx_http_write_filter(r, h_out);
    }

    //---------------------------------------------------------------------------------------------------------------------------------------------

    // 定义在ngx_http_write_filter_module.c
    // 真正的向客户端发送数据，调用send_chain
    // 如果数据发送不完，就保存在r->out里，返回again
    // 需要再次发生可写事件才能发送
    // 不是last、flush，且数据量较小（默认1460）
    // 那么这次就不真正调用write发送，减少系统调用的次数，提高性能
    // 在此函数里处理限速
    return ngx_http_write_filter(r, &out);//1010
}


// 初始化header过滤链表头节点，保证链表末尾不是空指针
// 此时头节点是ngx_http_header_filter
static ngx_int_t
ngx_http_header_filter_init(ngx_conf_t *cf)
{
    ngx_http_top_header_filter = ngx_http_header_filter;

    return NGX_OK;
}

