
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t  *mirror;
    ngx_flag_t    request_body;
} ngx_http_mirror_loc_conf_t;


typedef struct {
    ngx_int_t     status;
} ngx_http_mirror_ctx_t;


static ngx_int_t ngx_http_mirror_handler(ngx_http_request_t *r);
static void ngx_http_mirror_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_mirror_handler_internal(ngx_http_request_t *r);
static void *ngx_http_mirror_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_mirror_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_mirror(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_mirror_init(ngx_conf_t *cf);


static ngx_command_t  ngx_http_mirror_commands[] = {

    { ngx_string("mirror"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_mirror,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("mirror_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_mirror_loc_conf_t, request_body),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_mirror_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_mirror_init,                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_mirror_create_loc_conf,       /* create location configuration */
    ngx_http_mirror_merge_loc_conf         /* merge location configuration */
};


ngx_module_t  ngx_http_mirror_module = {
    NGX_MODULE_V1,
    &ngx_http_mirror_module_ctx,           /* module context */
    ngx_http_mirror_commands,              /* module directives */
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


static ngx_int_t
ngx_http_mirror_handler(ngx_http_request_t *r)
{
    ngx_int_t                    rc;
    ngx_http_mirror_ctx_t       *ctx;
    ngx_http_mirror_loc_conf_t  *mlcf;

    if (r != r->main) {
        return NGX_DECLINED;
    }

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_mirror_module);

    if (mlcf->mirror == NULL) {
        return NGX_DECLINED;
    }

    if (mlcf->request_body) {
        ctx = ngx_http_get_module_ctx(r, ngx_http_mirror_module);

        if (ctx) {
            return ctx->status;
        }

        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_mirror_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ctx->status = NGX_DONE;

        ngx_http_set_ctx(r, ctx, ngx_http_mirror_module);

        rc = ngx_http_read_client_request_body(r, ngx_http_mirror_body_handler);
        if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            return rc;
        }

        ngx_http_finalize_request(r, NGX_DONE);
        return NGX_DONE;
    }

    return ngx_http_mirror_handler_internal(r);
}


static void
ngx_http_mirror_body_handler(ngx_http_request_t *r)
{
    ngx_http_mirror_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_mirror_module);

    ctx->status = ngx_http_mirror_handler_internal(r);

    r->preserve_body = 1;

    r->write_event_handler = ngx_http_core_run_phases;
    ngx_http_core_run_phases(r);
}


static ngx_int_t
ngx_http_mirror_handler_internal(ngx_http_request_t *r)
{
    ngx_str_t                   *name;
    ngx_uint_t                   i;
    ngx_http_request_t          *sr,*pr,**ppr=NULL;
    ngx_http_mirror_loc_conf_t  *mlcf;
    ngx_http_post_subrequest_t  *psr;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_mirror_module);

    name = mlcf->mirror->elts;

    

    // �������������
    // psr = ngx_pcalloc(r->pool,sizeof(ngx_http_post_subrequest_t));////830 add
    // psr->handler=ngx_http_mirror1_post_subrequest_pt;//830 add
    // psr->data=name;
    psr=NULL;
    r->for_eagain=0;    

    for (i = 0; i < mlcf->mirror->nelts; i++) {
        if (ngx_http_subrequest(r, &name[i], &r->args, &sr, psr,
                                NGX_HTTP_SUBREQUEST_IN_MEMORY)!= NGX_OK)
        {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        // ������������ɣ�����û�п�ʼ����
        // �޸�������Ĳ���
        sr->header_only = 0;
        sr->method = r->method;
        sr->method_name = r->method_name;
        sr->subrequest_in_memory=0;
        sr->for_eagain=0;   
        sr->key = i+1;
        sr->un_wake = 1;
        sr->rfree = 0;
        sr->done = 0;
       	sr->send_last_chain = 0;
 
        for(pr = r->main; pr ;pr = pr->next){ 
            ppr = &pr->next;      
        }
        *ppr = sr;

    }

    r->main->mirror_num=i+1;
    r->main->abt_num = i+1;
    r->key = 0;
    r->main->rfree = 0;
    r->main->un_wake = 1;
    r->main->pre_abt_done = 0;
    r->main->abt_done = 0;

    ngx_flow_node_t *fl_nd,**ffn;
    ngx_int_t       k = 0;

    for ( i = 0; i < mlcf->mirror->nelts + 1; i++)
    {        
        ffn=&r->main->flow_node;
        k = 0;
        for (fl_nd = r->main->flow_node; fl_nd; fl_nd = fl_nd->next) 
        {
            k++;
            
            if( k > 2) return NGX_DECLINED;

            ffn = &fl_nd->next;
        }   
    
        fl_nd = ngx_palloc(r->main->pool, sizeof(ngx_flow_node_t));
        memset(fl_nd, 0, sizeof(ngx_flow_node_t));

        fl_nd->key = i;
        fl_nd->location_value.data = NULL;
        fl_nd->location_value.len = 0;
        fl_nd->header_data_ready = 0;
        fl_nd->header_data = NULL;
        fl_nd->status_code = 0;
        fl_nd->content_tp_hash = 0;
        fl_nd->content_len_n = -1;
        fl_nd->alert_sent = 0;
        fl_nd->b_arb_ready = 0;
        fl_nd->sum = 0;
        fl_nd->body_data_chain = NULL;
        fl_nd->bd_crc = 0;
        fl_nd->size = 0;
        fl_nd->last_flag = 0;
        fl_nd->vo_alert = 0;
        fl_nd->e_body = 1;    
        *ffn = fl_nd;
        ffn = &fl_nd->next;
        *ffn=NULL;  
    }
    
    r->main->abt302=1;
    return NGX_DECLINED;
}


static void *
ngx_http_mirror_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_mirror_loc_conf_t  *mlcf;

    mlcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_mirror_loc_conf_t));
    if (mlcf == NULL) {
        return NULL;
    }

    mlcf->mirror = NGX_CONF_UNSET_PTR;
    mlcf->request_body = NGX_CONF_UNSET;

    return mlcf;
}


static char *
ngx_http_mirror_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_mirror_loc_conf_t *prev = parent;
    ngx_http_mirror_loc_conf_t *conf = child;

    ngx_conf_merge_ptr_value(conf->mirror, prev->mirror, NULL);
    ngx_conf_merge_value(conf->request_body, prev->request_body, 1);

    return NGX_CONF_OK;
}


static char *
ngx_http_mirror(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_mirror_loc_conf_t *mlcf = conf;

    ngx_str_t  *value, *s;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        if (mlcf->mirror != NGX_CONF_UNSET_PTR) {
            return "is duplicate";
        }

        mlcf->mirror = NULL;
        return NGX_CONF_OK;
    }

    if (mlcf->mirror == NULL) {
        return "is duplicate";
    }

    if (mlcf->mirror == NGX_CONF_UNSET_PTR) {
        mlcf->mirror = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (mlcf->mirror == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    s = ngx_array_push(mlcf->mirror);
    if (s == NULL) {
        return NGX_CONF_ERROR;
    }

    *s = value[1];

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_mirror_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_mirror_handler;

    return NGX_OK;
}
