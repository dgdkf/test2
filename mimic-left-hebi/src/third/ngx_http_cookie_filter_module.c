#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

#define    MINIC  "/"
#define    MINIC1 "/mimic1"
#define    MINIC2 "/mimic2"
//作为cookie数据中的唯一标识---账号字段
#define    ID   "token"
#define    PNUM 3


//cookie相关的数据结构体

typedef struct {
    u_char      *ipport;
    u_char      *cookie;
}ngx_http_cookie_data_t;

typedef struct {
    //对于ngx_rbtree_node_t最后一个data成员
    u_char                          rbtree_node_data;
    ngx_int_t                       num;
    u_char                          *username;//节点用作字符串完全比对的账号
    u_char                          *ipport1;
    u_char                          *ipport2;
    u_char                          *ipport3;
    u_char                          *cookie1;
    u_char                          *cookie2;
    u_char                          *cookie3;
    //ngx_http_cookie_data_t          *cookies[3];
}ngx_http_cookie_data_node_t;


typedef struct{
    //用于快速检索的红黑树
    ngx_rbtree_t            rbtree;
    //使用红黑树必须定义哨兵节点
    ngx_rbtree_node_t       sentinel;
}ngx_http_cookie_shm_t;

typedef struct{
    ngx_str_t      *enable;
    ssize_t         shmsize;
    //操作共享内存一定要有ngx_slab_pool结构体
    //这个结构体也在共享内存中
    ngx_slab_pool_t *shpool;
    //指向共享内存中的ngx_http_cookie_shm_t结构体
    ngx_http_cookie_shm_t   *sh;
}ngx_http_cookie_filter_conf_t;

//http 上下文结构体，其中包括add_prefix整型成员，在处理http头部时用这个add_prefix表示在处理http处理胞体时是否添加前缀。
// typedef struct{
//     ngx_int_t  minic_cookie;//=0时，表示不需要添加胞体前缀，=1时表示需要添加确保提前缀，=2时表示已经添加过胞体前缀。
// }ngx_http_cookie_filter_ctx_t;


static char* ngx_http_cookie_create_slab(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_cookie_request_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_cookie_filter_shm_init(ngx_shm_zone_t *shm_zone, void *data);

static ngx_int_t ngx_http_cookie_filter_init(ngx_conf_t *cf);
static void* ngx_http_cookie_filter_create_conf(ngx_conf_t *cf);
//static char* ngx_http_cookie_filter_merge_conf(ngx_conf_t *cf,void *parent, void *child);
static ngx_int_t ngx_http_cookie_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_cookie_body_filter(ngx_http_request_t *r,ngx_chain_t *in);
static void ngx_http_cookie_rbtree_insert_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,ngx_rbtree_node_t *sentinel);
static ngx_rbtree_node_t* ngx_http_cookie_filter_find(ngx_http_request_t *r,ngx_http_cookie_filter_conf_t *conf,ngx_uint_t hash, ngx_str_t *account);
static ngx_int_t ngx_http_cookie_filter_insert_node(ngx_http_request_t *r,ngx_http_cookie_filter_conf_t *conf, ngx_rbtree_node_t *node, ngx_uint_t hash, ngx_str_t *account, ngx_str_t *padding_cookie);
//static ngx_int_t ngx_http_cookie_filter_delete_node(ngx_http_request_t *r,ngx_http_cookie_filter_conf_t *conf, ngx_rbtree_node_t *node);

static ngx_command_t ngx_http_cookie_filter_commands[]={
    {
        ngx_string("mimic_cookie"),
        NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE12,
        ngx_http_cookie_create_slab,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(ngx_http_cookie_filter_conf_t,enable),
        NULL},

    ngx_null_command
};


static ngx_http_module_t ngx_http_cookie_filter_module_ctx = {
    NULL,                                               /* preconfiguration */
    ngx_http_cookie_filter_init,                        /* postconfiguration */
 
    ngx_http_cookie_filter_create_conf,            /* create main configuration */
    NULL,                                               /* init main configuration */

    NULL,                                               /* create server configuration */
    NULL,                                               /* merge server configuration */

    NULL, //ngx_http_cookie_filter_create_conf,                 /* create location configuration */
    NULL//ngx_http_cookie_filter_merge_conf                   /* merge location configuration */
};
        
ngx_module_t ngx_http_cookie_filter_module={
    NGX_MODULE_V1,
    &ngx_http_cookie_filter_module_ctx,    /* module context */
    ngx_http_cookie_filter_commands,       /* module directives */
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

//初始化http模块
static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

static ngx_int_t ngx_http_cookie_filter_init(ngx_conf_t *cf){
    //设置请求头的处理阶段
    ngx_http_handler_pt             *h;
    ngx_http_core_main_conf_t       *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf,ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
    if(h == NULL)
        return NGX_ERROR;

    *h = ngx_http_cookie_request_handler;

    //设置过滤模块的位置
    //插入到头部的处理方法链表首部
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_cookie_header_filter;
    //插入到包体处理方法的首部
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_cookie_body_filter;

    return NGX_OK;
}

//创建共享内存
static char*
ngx_http_cookie_create_slab(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
    ngx_str_t                         *value;
    ngx_shm_zone_t                    *shm_zone;

    //conf ngx_http_cookie_filter_create_conf 创建的结构提
    ngx_http_cookie_filter_conf_t *mconf = (ngx_http_cookie_filter_conf_t *)conf;

    ngx_str_t    name = ngx_string("cookie_slab_shm");
    //获取配置文件的中minic_cookie的配置参数
    value = cf->args->elts;

    mconf->enable = ngx_pcalloc(cf->pool,sizeof(ngx_str_t));
    mconf->enable->len = value[1].len;
    mconf->enable->data = ngx_pcalloc(cf->pool,mconf->enable->len);
    ngx_memcpy(mconf->enable->data ,value[1].data,value[1].len);
    if(ngx_strncmp(value[1].data,"on",2) != 0 && ngx_strncmp(value[1].data,"off",3) != 0){
        return "invalid value";
    }

    //获取共享内存大小
    mconf->shmsize = ngx_parse_size(&value[2]);
    if(mconf->shmsize == (ssize_t)NGX_ERROR || mconf->shmsize ==0){
        return  "invalid shm value";
    }

    //要求nginx准备分配内存
    shm_zone = ngx_shared_memory_add(cf,&name,mconf->shmsize, &ngx_http_cookie_filter_module);
    if(shm_zone == NULL){
        return NGX_CONF_ERROR;
    }

    //设置共享内存
    shm_zone->init = ngx_http_cookie_filter_shm_init;
    shm_zone->data = mconf;

    return NGX_CONF_OK;
}

//共享内存加载且初始化红黑树
static ngx_int_t
ngx_http_cookie_filter_shm_init(ngx_shm_zone_t *shm_zone, void *data){
    ngx_http_cookie_filter_conf_t *conf;
    ngx_http_cookie_filter_conf_t *oconf = data;

    size_t                        len;
    conf = (ngx_http_cookie_filter_conf_t *)shm_zone->data;

    if(oconf){
        //data 是上次创建的ngx_http_cookie_filter_conf_t
        conf->sh = oconf->sh;
        conf->shpool = oconf->shpool;
        return NGX_OK;
    }

    //存放内存的首地址
    conf->shpool = (ngx_slab_pool_t*)shm_zone->shm.addr;

    //slab共享内存每一次分配的内存都用于存放 ngx_http_cookie_shm_t 红黑树
    conf->sh = ngx_slab_alloc(conf->shpool,sizeof(ngx_http_cookie_shm_t));
    if(conf->sh == NULL){
        return NGX_ERROR;
    }

    conf->shpool->data =  conf->sh;

    //初始化红黑树
    ngx_rbtree_init(&conf->sh->rbtree, &conf->sh->sentinel,ngx_http_cookie_rbtree_insert_value);

    //slab 操作内存出错时
    len = sizeof(" in slab \" \"")+shm_zone->shm.name.len;

    conf->shpool->log_ctx = ngx_slab_alloc(conf->shpool,len);
    if(conf->shpool->log_ctx == NULL){
        return NGX_ERROR;
    }

    ngx_sprintf(conf->shpool->log_ctx, " in cookieslab \"%V \"%Z", &shm_zone->shm.name);

    return NGX_OK;
}

//红黑树初始化的回调函数
static void ngx_http_cookie_rbtree_insert_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,ngx_rbtree_node_t *sentinel){
    ngx_rbtree_node_t       **p;
    ngx_http_cookie_data_node_t  *lrn, *lrnt;

    for (; ; ){
        if(node->key < temp->key){
            p = &temp->left;
        }else if (node->key > temp->key){
            p = &temp->right;
        }else{
            //从data成员开始就是ngx_http_cookie_data_t的结构体了
            lrn = (ngx_http_cookie_data_node_t*)&node->data;
            lrnt = (ngx_http_cookie_data_node_t*)&temp->data;
            //针对username字符串完全比较
            p = ngx_memn2cmp(lrn->username, lrnt->username, ngx_strlen(lrn->username)-1, ngx_strlen(lrnt->username)-1 < 0)? &temp->left: &temp->right;
        }

        if(*p == sentinel){
            break;
        }
        temp = *p;
    }

    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
    
}
//返回中超找到的节点指针,没找到返回根节点
static ngx_rbtree_node_t* ngx_http_cookie_filter_find(ngx_http_request_t *r,ngx_http_cookie_filter_conf_t *conf,ngx_uint_t hash, ngx_str_t *account){
    ngx_rbtree_node_t                   *node,*sentinel;
    ngx_http_cookie_data_node_t         *lr;
    
    ngx_int_t                           rc;

    node = conf->sh->rbtree.root;
    sentinel = conf->sh->rbtree.sentinel;
    while (node != sentinel){
        if(hash > node->key){
            node = node->left;
            continue;
        }

        if (hash > node->key){
            node = node->right;
            continue;
        }

        //hash == node->key
        lr = (ngx_http_cookie_data_node_t *)&node->data;
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_find\"%s---%V\"",lr->username,account);
        rc = ngx_strncmp(lr->username, account->data, account->len);
        if(rc == 0){
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_find the node\"%s---%V\" ",lr->username,account);
            break;
        }
        node = (rc < 0)? node->left: node->right; 
    }
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_find return  node\"%ud %ud\"",node->key,hash);
    return node;
}

//将找到的节点插入到红黑树中
static ngx_int_t ngx_http_cookie_filter_insert_node(ngx_http_request_t *r,ngx_http_cookie_filter_conf_t *conf, ngx_rbtree_node_t *node, ngx_uint_t hash, ngx_str_t *account, ngx_str_t *padding_cookie){
    size_t                              size;
    ngx_http_cookie_data_node_t         *lr;
    ngx_rbtree_node_t                   *node_data;
    ngx_http_core_loc_conf_t            *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (!r->internal && clcf->internal) {
        ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
        return NGX_OK;
    }
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "ngx_http_cookie_header_filter using configuration \"%s%V\"",
                   (clcf->noname ? "*" : (clcf->exact_match ? "=" : "")),
                   &clcf->name);

    //ngx_uint_t hash = ngx_crc32_short(account->data,account->len);

    if(node->key != hash){//find函数返回的节点不是找到的节点
        //首先获得连续的内存块,ngx_http_cookie_data_node_t结构体大小+username的长度+3倍的iport + 3倍的cookie
        size = offsetof(ngx_rbtree_node_t,data)+ offsetof(ngx_http_cookie_data_node_t,num) + offsetof(ngx_http_cookie_data_node_t,username) + offsetof(ngx_http_cookie_data_node_t,ipport1) + offsetof(ngx_http_cookie_data_node_t,ipport2) + offsetof(ngx_http_cookie_data_node_t,ipport3) + offsetof(ngx_http_cookie_data_node_t,cookie1) + offsetof(ngx_http_cookie_data_node_t,cookie2) + offsetof(ngx_http_cookie_data_node_t,cookie3);
        //首先淘汰过期的node
        //ngx_http_cookie_filter_expire();

        node_data = ngx_slab_alloc_locked(conf->shpool,size);
        if(node_data == NULL){
            return NGX_ERROR;
        }

        node_data->key = hash;
        lr = (ngx_http_cookie_data_node_t*)&node_data->data;
        lr->username = ngx_slab_alloc_locked(conf->shpool,account->len);
        ngx_memcpy(lr->username, account->data, account->len);

        if(ngx_strcmp(clcf->name.data,MINIC) == 0){
            //ngx_int_t ipport1_len = ngx_strlen(r->upstream->peer.name->data);
            lr->ipport1 = ngx_slab_alloc_locked(conf->shpool,clcf->name.len);
            ngx_memcpy(lr->ipport1, clcf->name.data, clcf->name.len);
            lr->cookie1 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            ngx_memcpy(lr->cookie1, padding_cookie->data, padding_cookie->len);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_insert_node1\"%V\"",r->upstream->peer.name);

        }else if(ngx_strcmp(clcf->name.data,MINIC1) == 0){
            lr->ipport2 = ngx_slab_alloc_locked(conf->shpool,clcf->name.len);
            ngx_memcpy(lr->ipport2, clcf->name.data, clcf->name.len);
            lr->cookie2 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            ngx_memcpy(lr->cookie2, padding_cookie->data, padding_cookie->len);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_insert_node2\"%V\"",r->upstream->peer.name);

        }else if(ngx_strcmp(clcf->name.data,MINIC2) == 0){
            lr->ipport3 = ngx_slab_alloc_locked(conf->shpool,clcf->name.len);
            ngx_memcpy(lr->ipport3, clcf->name.data, clcf->name.len);
            lr->cookie3 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            ngx_memcpy(lr->cookie3, padding_cookie->data, padding_cookie->len);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_insert_node3\"%V\"",r->upstream->peer.name);
        } 


            // ngx_int_t ipport1_len = ngx_strlen(r->upstream->peer.name->data);
            // lr->ipport1 = ngx_slab_alloc_locked(conf->shpool,ipport1_len);
            // ngx_memcpy(lr->ipport1, r->upstream->peer.name->data, ipport1_len);
            // lr->cookie1 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            // ngx_memcpy(lr->cookie1, padding_cookie->data, padding_cookie->len);
            // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
            //                "ngx_http_cookie_filter_insert_node1\"%V\"",r->upstream->peer.name);
        lr->num = 1;
        ngx_rbtree_insert(&conf->sh->rbtree,node_data);
    }else{//表示find函数返回的节点是找到的节点
        lr = (ngx_http_cookie_data_node_t*)&node->data;

        // ngx_int_t account_len = ngx_strlen(account);
        // lr->username = ngx_slab_alloc_locked(conf->shpool,account_len);
        // ngx_memcpy(lr->username, account, ngx_strlen(account));
        if(ngx_strcmp(clcf->name.data,MINIC) == 0){
            //ngx_int_t ipport1_len = ngx_strlen(r->upstream->peer.name->data);
            lr->ipport1 = ngx_slab_alloc_locked(conf->shpool,clcf->name.len);
            ngx_memcpy(lr->ipport1, clcf->name.data, clcf->name.len);
            lr->cookie1 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            ngx_memcpy(lr->cookie1, padding_cookie->data, padding_cookie->len);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_insert_node1\"%V\"",r->upstream->peer.name);

        }else if(ngx_strcmp(clcf->name.data,MINIC1) == 0){
            lr->ipport2 = ngx_slab_alloc_locked(conf->shpool,clcf->name.len);
            ngx_memcpy(lr->ipport2, clcf->name.data, clcf->name.len);
            lr->cookie2 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            ngx_memcpy(lr->cookie2, padding_cookie->data, padding_cookie->len);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_insert_node2\"%V\"",r->upstream->peer.name);

        }else if(ngx_strcmp(clcf->name.data,MINIC2) == 0){
            lr->ipport3 = ngx_slab_alloc_locked(conf->shpool,clcf->name.len);
            ngx_memcpy(lr->ipport3, clcf->name.data, clcf->name.len);
            lr->cookie3 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
            ngx_memcpy(lr->cookie3, padding_cookie->data, padding_cookie->len);
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_filter_insert_node3\"%V\"",r->upstream->peer.name);
        }
        lr->num += 1;
        // if(lr->ipport2 ==NULL  && ngx_strcmp(lr->ipport1,r->upstream->peer.name->data) != 0 ){
        //     ngx_int_t ipport2_len = ngx_strlen(r->upstream->peer.name->data);
        //     lr->ipport2 = ngx_slab_alloc_locked(conf->shpool,ipport2_len);
        //     ngx_memcpy(lr->ipport2, r->upstream->peer.name->data, ipport2_len);
        //     lr->cookie2 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
        //     ngx_memcpy(lr->cookie2, padding_cookie->data, padding_cookie->len);
        //     ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
        //                    "ngx_http_cookie_filter_insert_node2\"%V\"",r->upstream->peer.name);
        // }else if(ngx_strcmp(lr->ipport1,r->upstream->peer.name->data) == 0){
        //     ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
        //                    "ngx_http_cookie_filter_insert_node1 nothing\"%V\"",r->upstream->peer.name);
        // }else if(lr->ipport3 ==NULL && ngx_strcmp(lr->ipport2,r->upstream->peer.name->data) != 0 ){
        //     ngx_int_t ipport3_len = ngx_strlen(r->upstream->peer.name->data);
        //     lr->ipport3 = ngx_slab_alloc_locked(conf->shpool,ipport3_len);
        //     ngx_memcpy(lr->ipport3, r->upstream->peer.name->data, ipport3_len);
        //     lr->cookie3 = ngx_slab_alloc_locked(conf->shpool,padding_cookie->len);
        //     ngx_memcpy(lr->cookie3, padding_cookie->data, padding_cookie->len);
        //     ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
        //                    "ngx_http_cookie_filter_insert_node3\"%V\"",r->upstream->peer.name);
        // }else if(ngx_strcmp(lr->ipport3,r->upstream->peer.name->data) == 0){
        //     ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
        //                    "ngx_http_cookie_filter_insert_node3 nothing\"%V\"",r->upstream->peer.name);
        // }else{
        //     //报错ipport 不存在
        //     return NGX_ERROR;
        // }

        //ngx_rbtree_insert(&conf->sh->rbtree,node);更新数据，不用增添新的节点
    }
    

    return NGX_OK;
}


//HTTP请求头的处理
//如果headerz中存在Cookie字段，并且该字段的数据中包含username键值对，则进行处理，否则不进行处理
static ngx_int_t ngx_http_cookie_request_handler(ngx_http_request_t *r){
    ngx_int_t                       rc;
    ngx_uint_t                      i;
    ngx_http_cookie_filter_conf_t   *conf;
    ngx_rbtree_node_t               *node;
    ngx_http_cookie_data_node_t     *lr;
    ngx_http_core_loc_conf_t        *clcf;

    conf = ngx_http_get_module_main_conf(r,ngx_http_cookie_filter_module);
    rc = NGX_DECLINED;
    if(conf->enable == NGX_CONF_UNSET || ngx_strncmp(conf->enable->data,"off",3) == 0){
        return rc;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (!r->internal && clcf->internal) {
        ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
        return NGX_OK;
    }
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "using configuration \"%s%V\"",
                   (clcf->noname ? "*" : (clcf->exact_match ? "=" : "")),
                   &clcf->name);
    
    ngx_list_part_t *part = &r->headers_in.headers.part;
    ngx_table_elt_t *header = part->elts;
    //ngx_list_part_t *pre_part, *now_part;
    for(i = 0; ; i++){
        //判断是否到达链表结尾处
        if(i >= part->nelts){
            //判断是否还有下一个链表数组元素
            if(part->next == NULL){
                break;
            }
            //part设置为next来访问的下一个链表数组；header指向笑一个链表数组的首地址，i设为0,表示从头开始遍历
            //pre_part = part;//上一个节点
            part = part->next;
            header = part->elts;
            i = 0;
        }
        if(header[i].hash == 0){
            //hash=0表示不合法的头部
            continue;
        }
        //判断是否Set-Cookie，如果忽略大小写，则用lowcase_key
        if( 0 == ngx_strncasecmp(header[i].key.data, (u_char*)"Cookie",header[i].key.len)){
            ngx_str_t *cookie_value = ngx_palloc(r->pool,sizeof(ngx_str_t));
            cookie_value->len = header[i].value.len;
            cookie_value->data = ngx_palloc(r->pool, cookie_value->len);
            ngx_memcpy(cookie_value->data, header[i].value.data, cookie_value->len);
            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_request_handler %ud \"%V: %V \"",i,
                           &header[i].key, cookie_value);
            //逻辑流程如下
            //
            //1.先找出username账号信息
            //2.根据account信息找到对应的cookie节点信息
            //3.根据前往的目的地址，先择不同的cookie进行替换(难点是目的地址很难获取)已解决clcf->name可以区分前往的目的地
            //开始编码
            //1.截取出account信息
            ngx_str_t *account = ngx_palloc(r->pool,sizeof(ngx_str_t));
            u_char *account_pos =  ngx_strnstr(header[i].value.data,ID,header[i].value.len);
            //
            // u_char *account_pos1 =  ngx_strstrn(account_pos,ID,9-1);
            // if(account_pos1){//账号与账号表示相同，报错
            //     ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"account is username, please change the account!");
            //     return NGX_ERROR;
            // }
            if(account_pos){//存在username字段的cookie
                u_char *end_pos = (u_char *)ngx_strchr(account_pos,';');
                ssize_t account_len = end_pos - account_pos;
                account->len = account_len;
                account->data = ngx_palloc(r->pool,account->len);
                ngx_memcpy(account->data,account_pos,account->len);
                ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_request_handler account is %V",account);
            
                // //2.cookie节点的寻找
                u_int32_t hash = ngx_crc32_short(account->data,account->len);
                ngx_shmtx_lock(&conf->shpool->mutex);
                node = ngx_http_cookie_filter_find(r,conf,hash,account);
                ngx_shmtx_unlock(&conf->shpool->mutex);
                if(node->key != hash){
                    ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_request_handler can not find currectely node");
                    return rc;
                }else{
                    lr = (ngx_http_cookie_data_node_t*)&node->data;
                }
                ngx_log_debug(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_request_handler find cookie");

                //3.cokie的替换
                if(ngx_strcmp(clcf->name.data,MINIC) == 0){
                    if(lr && lr->cookie1 != NULL){
                        //cookie->hash = 123;
                        //ngx_str_set(&header[i].value,lr->cookie1);
                        header[i].value.len = ngx_strlen(lr->cookie1)-1;
                        ngx_memcpy(header[i].value.data, lr->cookie1, header[i].value.len);
                        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_request_handler find cookie1 %V---%s",&header[i].value,lr->cookie1);
                        //cookie->lowcase_key = "c";
                    }
                }else if(ngx_strcmp(clcf->name.data,MINIC1) == 0){
                    if(lr && lr->cookie2 != NULL){
                        //cookie->hash = 123;
                        //ngx_str_set(&header[i].value,lr->cookie2);
                        header[i].value.len = ngx_strlen(lr->cookie2)-1;
                        ngx_memcpy(header[i].value.data, lr->cookie2, header[i].value.len);
                        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_request_handler find cookie2 %V---%s",&header[i].value,lr->cookie2);
                        //cookie->lowcase_key = "c";
                    }
                }else if(ngx_strcmp(clcf->name.data,MINIC2) == 0){
                    if(lr && lr->cookie3 != NULL){
                        //cookie->hash = 123;
                        //ngx_str_set(&header[i].value,lr->cookie3);
                        header[i].value.len = ngx_strlen(lr->cookie3)-1;
                        ngx_memcpy(header[i].value.data, lr->cookie3, header[i].value.len);
                        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_request_handler find cookie3 %V---%s",&header[i].value,lr->cookie3);
                        //cookie->lowcase_key = "c";
                    }
                }
                ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                            "ngx_http_cookie_request_handler replace ok %ud \"%V: %V \"",i,
                            &header[i].key, &header[i].value);
            }
        }
    }
    return rc;
}


//我们需要吧http响应包体前加的字符串前缀硬编码为filter_prefix变量
// static ngx_str_t filter_prefix = ngx_string("[my filter prefix]");
static ngx_int_t ngx_http_cookie_header_filter(ngx_http_request_t *r){
    ngx_uint_t                           i;
    ngx_http_cookie_filter_conf_t       *conf;
    ngx_str_t *padding_cookie = ngx_palloc(r->pool,sizeof(ngx_str_t));
    ngx_str_null(padding_cookie);
    ngx_str_t *account_name = ngx_palloc(r->pool,sizeof(ngx_str_t));
    ngx_str_null(account_name);

   //获取存储配置项的ngx_http_cookie_filter_conf_t结构体
   conf = ngx_http_get_module_main_conf(r, ngx_http_cookie_filter_module);
   if(conf == NULL || conf->enable== NULL || ngx_strncmp(conf->enable->data,"off",3) == 0){
       return ngx_http_next_header_filter(r);//minic_cookie 配置项没有配置，或者minic_cookie = off直接交由下一个过滤模块处理
   }

   /*
   遍历响应头
   */
    ngx_list_part_t *part = &r->headers_out.headers.part;
    ngx_table_elt_t *header = part->elts;
    for(i = 0; ; i++){
        //判断是否到达链表结尾处
        if(i>= part->nelts){
            //判断是否还有下一个链表数组元素
            if(part->next == NULL){
                break;
            }
            //part设置为next来访问的下一个链表数组；header指向笑一个链表数组的首地址，i设为0,表示从头开始遍历
            part = part->next;
            header = part->elts;
            i = 0;
        }
        if(header[i].hash == 0){
            //hash=0表示不合法的头部
            continue;
        }
        //判断是否Set-Cookie，如果忽略大小写，则用lowcase_key
        if( 0 == ngx_strncasecmp(header[i].key.data, (u_char*)"Set-Cookie", header[i].key.len)){
            //判断http头部是否包含username账户信息，有继续往下执行，没有放过，
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "ngx_http_cookie_header_filter\"%V: %V\"",
                           &header[i].key, &header[i].value);
            //Add By Semon(全局存储执行体相应数据中的Set-Cookie数据，并且使用第一个接受到的响应数据Set-Cookie替换后来的Set-Cookie数据)
            u_char* pos =  (u_char*)ngx_strchr(header[i].value.data,';');
            ngx_int_t totle_len = header[i].value.len;//totle letters
            //ngx_int_t flag = 1;
            if(pos){
                ngx_int_t pos_len = ngx_strlen(pos) ;//;字符后面的长度 
                ngx_int_t len = totle_len - pos_len ;
                if(!padding_cookie->data){
                    padding_cookie->len = len ;//第一个分号前的数据长度
                    padding_cookie->data = ngx_palloc(r->pool, padding_cookie->len);
                    ngx_memcpy(padding_cookie->data, header[i].value.data ,padding_cookie->len);
                }else{
                    size_t old_padding_cookie_len = padding_cookie->len;
                    u_char *old_padding_cookie_data = padding_cookie->data;
                    padding_cookie->len = old_padding_cookie_len + len +2; //2是分号加空格
                    padding_cookie->data = ngx_palloc(r->pool,padding_cookie->len);
                    ngx_memcpy(padding_cookie->data , old_padding_cookie_data, old_padding_cookie_len);
                    ngx_str_t tmp = ngx_string("; ");
                    ngx_memcpy(padding_cookie->data +old_padding_cookie_len , tmp.data, tmp.len);
                    ngx_memcpy(padding_cookie->data + old_padding_cookie_len + tmp.len , header[i].value.data, len);
                }
                ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_cookie_header_filter first K-V:\"%V\"", &header[i].value);
                //使用username+账号名作为唯一key
                u_char* account_pos = (u_char*)ngx_strnstr(header[i].value.data,ID,header[i].value.len);
                if(account_pos){
                    account_name->len = len;
                    account_name->data = ngx_palloc(r->pool,account_name->len);
                    ngx_memcpy(account_name->data, header[i].value.data,account_name->len);
                }
                //padding_cookie->len -= 1;//数据最后一个; 去掉
            }
        }
    }
    if(padding_cookie->len > 0){
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_cookie_header_filter padding cookie\"%V\"", padding_cookie);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_cookie_header_filter account_name\"%V\"", account_name);

        ngx_uint_t hash = ngx_crc32_short(account_name->data,account_name->len);
        ngx_rbtree_node_t* cookie_node = ngx_http_cookie_filter_find(r, conf, hash, account_name);
        ngx_http_cookie_data_node_t         *lr = (ngx_http_cookie_data_node_t*)&cookie_node->data;
        if(cookie_node->key == hash && lr->num >= PNUM){
            //do nothing
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"ngx_http_cookie_header_filter login account\"%V\"", account_name);
        }else{
            ngx_http_cookie_filter_insert_node(r,conf,cookie_node,hash,account_name,padding_cookie);
        }
    }
    return ngx_http_next_header_filter(r);//交由下一个过滤模块处理
}

static ngx_int_t ngx_http_cookie_body_filter(ngx_http_request_t *r,ngx_chain_t *in){
    // ngx_http_cookie_filter_ctx_t *ctx;
    // ctx = ngx_http_get_module_ctx(r,ngx_http_cookie_filter_module);
    // //获取不到上下文或者minic_cookie=1 | 2 都不用添加前缀
    // if(ctx == NULL || ctx->minic_cookie != 1){
    //     return ngx_http_next_body_filter(r,in);
    // }

    // //先设置为2，这样即使重新回调该模块，也不会添加前缀
    // ctx->minic_cookie = 2;

    // ngx_buf_t *b = ngx_create_temp_buf(r->pool,filter_prefix.len);
    // //将b的pos与last指针指向正确的位置，
    // b->start = b->pos = filter_prefix.data;
    // b->last = b->pos+filter_prefix.len;

    // //从内存池中生成ngx_chain_t链表，将刚分配的ngx_buf_t设置到buf成员中，并将它添加到原先待发送的http胞体前面。
    // ngx_chain_t *cl = ngx_alloc_chain_link(r->pool);
    // cl->buf = b;
    // cl->next = in;

    // return ngx_http_next_body_filter(r,cl);
    return ngx_http_next_body_filter(r,in);

}


static void* ngx_http_cookie_filter_create_conf(ngx_conf_t *cf){
    ngx_http_cookie_filter_conf_t *ccf;
    //创建存储配置项的结构体
    ccf = (ngx_http_cookie_filter_conf_t *)ngx_palloc(cf->pool, sizeof(ngx_http_cookie_filter_conf_t));
    if(ccf == NULL){
        return NULL;
    }

    //ngx_flat_t类型的变量，如果使用预设函数ngx_conf_set_flag_slot解析配置项参数，那么必须初始化为NGX_CONF_UNSET
    ccf->enable = NGX_CONF_UNSET;
    ccf->shmsize = NGX_CONF_UNSET;

    return ccf;
}


// static char* ngx_http_cookie_filter_merge_conf(ngx_conf_t *cf,void *parent, void *child){
//     ngx_http_cookie_filter_conf_t *prev = (ngx_http_cookie_filter_conf_t*) parent;
//     ngx_http_cookie_filter_conf_t *conf = (ngx_http_cookie_filter_conf_t*) child;

//     //合并ngx_flat_t类型的配置项
//     ngx_conf_merge_value(conf->enable,prev->enable,0);

//     return NGX_CONF_OK;
// }