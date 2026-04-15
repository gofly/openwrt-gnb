/*
   Copyright (C) gnbdev

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) || defined(__OpenBSD__)
#define __UNIX_LIKE_OS__ 1
#endif

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>

#ifdef __UNIX_LIKE_OS__
#include <arpa/inet.h>
#include <netdb.h>
#endif

#if defined(_WIN32)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "gnb_node_type.h"
#include "gnb_address.h"
#include "gnb_es_type.h"
#include "gnb_http_client.h"
#include "gnb_utils.h"
#include "jsmn/jsmn.h"

int gnb_test_field_separator(char *config_string);

static void gnb_do_resolv_node_address(gnb_es_ctx *es_ctx, gnb_node_t *node, char *host_string, uint16_t port, const char *dns_type){
    #define MAX_TOKENS 128
    jsmn_parser p;
    jsmntok_t t[MAX_TOKENS];
    int r, i;

    char path[512];
    char response_buf[2048];
    int response_len;
    gnb_address_t address_st;
    gnb_address_list_t *resolv_address_list;
    gnb_log_ctx_t *log = es_ctx->log;

    resolv_address_list = (gnb_address_list_t *)&node->resolv_address_block;
    
    GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "resolve [%s] type [%s] via DoH API on %s:%d\n",
        host_string, dns_type, es_ctx->doh_host, es_ctx->doh_port );

    // 构建符合Google DoH API规范的请求路径
    snprintf(path, sizeof(path), "/resolve?name=%s&type=%s", host_string, dns_type);

    // 调用HTTPS GET请求，使用配置的DoH服务器
    response_len = gnb_http_get(es_ctx->doh_host, es_ctx->doh_port, 1, path, response_buf, sizeof(response_buf));

    if (response_len <= 0) {
        GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "resolve [%s] failed, response empty\n", host_string);
        return;
    }

    jsmn_init(&p);
    r = jsmn_parse(&p, response_buf, response_len, t, MAX_TOKENS);
    if (r < 0) {
        GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "failed to parse JSON: %d\n", r);
        return;
    }

    // 查找 "Answer" 数组
    for (i = 1; i < r; i++) {
        if (t[i].type == JSMN_STRING && (int)strlen("Answer") == t[i].end - t[i].start &&
            strncmp(response_buf + t[i].start, "Answer", t[i].end - t[i].start) == 0) {

            jsmntok_t *arr_tok = &t[i + 1];
            if (i + 1 < r && arr_tok->type == JSMN_ARRAY) {
                int j;
                int current_tok_idx = i + 2;
                // 遍历Answer数组中的每个对象
                for (j = 0; j < arr_tok->size; j++) {
                    jsmntok_t *obj_tok = &t[current_tok_idx];

                    // 为每个对象初始化type和data
                    int dns_type = 0;
                    char ip_str[INET6_ADDRSTRLEN] = {0};

                    int k;
                    // 遍历对象中的键值对
                    for (k = 0; k < obj_tok->size; k++) {
                        jsmntok_t *key_tok = obj_tok + 1 + k * 2;
                        jsmntok_t *val_tok = key_tok + 1;

                        if (key_tok->type == JSMN_STRING && (int)strlen("data") == key_tok->end - key_tok->start &&
                            strncmp(response_buf + key_tok->start, "data", key_tok->end - key_tok->start) == 0) {
                            
                            int ip_len = val_tok->end - val_tok->start;
                            if (ip_len < sizeof(ip_str)) {
                                strncpy(ip_str, response_buf + val_tok->start, ip_len);
                                ip_str[ip_len] = '\0';
                            }
                        } else if (key_tok->type == JSMN_STRING && (int)strlen("type") == key_tok->end - key_tok->start &&
                            strncmp(response_buf + key_tok->start, "type", key_tok->end - key_tok->start) == 0) {
                            
                            char type_str[16];
                            int type_len = val_tok->end - val_tok->start;
                            if (type_len < sizeof(type_str)) {
                                strncpy(type_str, response_buf + val_tok->start, type_len);
                                type_str[type_len] = '\0';
                                dns_type = atoi(type_str);
                            }
                        }
                    }

                    // 在处理完一个对象的所有键值对后，根据type和data进行处理
                    if (ip_str[0] != '\0') {
                        memset(&address_st, 0, sizeof(gnb_address_t));
                        address_st.port = htons(port);
                        if (dns_type == 1) { // A record (IPv4)
                            if (inet_pton(AF_INET, ip_str, &address_st.m_address4) == 1) {
                                address_st.type = AF_INET;
                                gnb_address_list_update(resolv_address_list, &address_st);
                                GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "update [%s] type [%s]>[%s]\n",
                                    host_string, "A", GNB_IP_PORT_STR1(&address_st));
                            }
                        } else if (dns_type == 28) { // AAAA record (IPv6)
                            if (inet_pton(AF_INET6, ip_str, &address_st.m_address6) == 1) {
                                address_st.type = AF_INET6;
                                gnb_address_list_update(resolv_address_list, &address_st);
                                GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "update [%s] type [%s]>[%s]\n",
                                    host_string, "AAAA", GNB_IP_PORT_STR1(&address_st));
                            }
                        }
                    }

                    // Advance to the next object in the array
                    current_tok_idx += 1 + (obj_tok->size * 2);
                }
            }
            break; 
        }
    }
}

void gnb_resolv_address(gnb_es_ctx *es_ctx){
    char *conf_dir = es_ctx->ctl_block->conf_zone->conf_st.conf_dir;
    char address_file[PATH_MAX+NAME_MAX];

    snprintf(address_file, PATH_MAX+NAME_MAX, "%s/%s", conf_dir, "address.conf");

    // 在解析前清空所有节点的 resolv_address_block
    int i, j;
    int node_num = es_ctx->ctl_block->node_zone->node_num;
    gnb_address_list_t *resolv_address_list;
    gnb_node_t *node;
    gnb_address_t *gnb_address;
    gnb_log_ctx_t *log = es_ctx->log;

    for (i = 0; i < node_num; i++) {
        node = &es_ctx->ctl_block->node_zone->node[i];
        if (node) {
            resolv_address_list = (gnb_address_list_t *)&node->resolv_address_block;
            resolv_address_list->num = 0;
            for( j = 0; j < resolv_address_list->size; j++ ) {
                gnb_address = &resolv_address_list->array[j];
                gnb_address->port = 0;
            }
        }
    }

    FILE *file;
    file = fopen(address_file,"r");
    if ( NULL==file ) {
        return;
    }

    char line_buffer[1024+1];
    char attrib_string[16+1];
    gnb_uuid_t uuid64;
    char host_string[256+1];
    uint16_t port = 0;
    int num;

    do{

        num = fscanf(file,"%1024s\n",line_buffer);

        if ( EOF == num ) {
            break;
        }

        if ('#' == line_buffer[0]) {
            continue;
        }

        int ret = gnb_test_field_separator(line_buffer);

        if ( GNB_CONF_FIELD_SEPARATOR_TYPE_SLASH == ret ) {
            num = sscanf(line_buffer,"%16[^/]/%llu/%256[^/]/%hu\n", attrib_string, &uuid64, host_string, &port);
        } else if ( GNB_CONF_FIELD_SEPARATOR_TYPE_VERTICAL == ret ) {
            num = sscanf(line_buffer,"%16[^|]|%llu|%256[^|]|%hu\n", attrib_string, &uuid64, host_string, &port);
        } else {
            num = 0;
        }

        if ( 4 != num ) {
            continue;
        }

        if ( 0 == port ) {
            continue;
        }

        if ( NULL == check_domain_name(host_string) ) {
            continue;
        }

        node = (gnb_node_t *)GNB_HASH32_UINT64_GET_PTR(es_ctx->uuid_node_map, uuid64);

        if ( NULL == node ) {
            continue;
        }

        resolv_address_list = (gnb_address_list_t *)&node->resolv_address_block;

        const char *dns_type = "A";
        gnb_do_resolv_node_address(es_ctx, node, host_string, port, dns_type);
        
        dns_type = "AAAA";
        gnb_do_resolv_node_address(es_ctx, node, host_string, port, dns_type);

        // print resolv address list
        for( i = 0; i < resolv_address_list->size; i++ ) {
            gnb_address = &resolv_address_list->array[i];
            if (0==gnb_address->port) {
                continue;
            }
    
            GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "resolved [%s]>[%s]\n", host_string, GNB_IP_PORT_STR1(gnb_address));
        }

    } while(1);

    fclose(file);

}

/*
dig -6 TXT +short o-o.myaddr.l.google.com @ns1.google.com | awk -F'"' '{ print $2}'
*/
void gnb_load_wan_ipv6_address(gnb_es_ctx *es_ctx){

    gnb_log_ctx_t *log = es_ctx->log;

    if ( NULL == es_ctx->wan_address6_file ) {
        return;
    }

    FILE *file;

    file = fopen(es_ctx->wan_address6_file,"r");

    if ( NULL == file ) {
        return;
    }

    gnb_conf_t *conf = &es_ctx->ctl_block->conf_zone->conf_st;

    char host_string[46+1];

    int num;

    num = fscanf(file,"%46s\n",host_string);

    int s;

    s = inet_pton(AF_INET6, host_string, (struct in_addr *)&es_ctx->ctl_block->core_zone->wan6_addr);

    if (s <= 0) {
        memset(&es_ctx->ctl_block->core_zone->wan6_addr,0,16);
        es_ctx->ctl_block->core_zone->wan6_port = 0;
    } else {
        es_ctx->ctl_block->core_zone->wan6_port = htons(conf->udp6_ports[0]);
        GNB_LOG1(log, GNB_LOG_ID_ES_RESOLV, "load wan address6[%s:%d]\n", host_string, conf->udp6_ports[0]);
    }

}
