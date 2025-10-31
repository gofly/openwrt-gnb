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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include "gnb.h"
#include "gnb_worker.h"
#include "gnb_ctl_block.h"

#define BUFFER_SIZE 4096

typedef struct _exporter_worker_ctx_t {
    gnb_core_t *gnb_core;
    pthread_t thread_worker;
    int server_fd;
} exporter_worker_ctx_t;

// Function to generate the Prometheus metrics string
static void generate_metrics_string(char *buffer, size_t buffer_size, gnb_ctl_block_t *ctl_block) {
    size_t offset = 0;
    const char *network;
    int ret;

    network = (const char *)ctl_block->core_zone->ifname;
    const char *hyphen = strchr(network, '-');
    if (hyphen != NULL) {
        network = hyphen + 1;
    }

    uint8_t instance_up = 0;
    if (time(NULL) - ctl_block->status_zone->keep_alive_ts_sec < 5) {
        instance_up = 1;
    }

    ret = snprintf(buffer + offset, buffer_size - offset,
        "gnb_instance_up{network=\"%s\"} %"PRIu8"\n",
        network, instance_up
    );
    if (ret < 0 || ret >= buffer_size - offset) return;
    offset += ret;

    gnb_node_t *node;
    size_t node_num = ctl_block->node_zone->node_num;

    for (size_t i = 0; i < node_num; i++) {
        node = &ctl_block->node_zone->node[i];
        if (!((GNB_NODE_STATUS_IPV6_PONG | GNB_NODE_STATUS_IPV4_PONG) & node->udp_addr_status)) continue;

        uint8_t state = 0;
        if (GNB_NODE_STATUS_IPV4_PONG & node->udp_addr_status) state |= 1;
        if (GNB_NODE_STATUS_IPV6_PONG & node->udp_addr_status) state |= 2;

        ret = snprintf(buffer + offset, buffer_size - offset,
            "gnb_node_state{network=\"%s\",node=\"%"PRIu64"\"} %"PRIu8"\n",
            network, node->uuid64, state
        );
        if (ret < 0 || ret >= buffer_size - offset) break;
        offset += ret;

        ret = snprintf(buffer + offset, buffer_size - offset,
            "gnb_node_detect_count{network=\"%s\",node=\"%"PRIu64"\"} %"PRIu32"\n",
            network, node->uuid64, node->detect_count
        );
        if (ret < 0 || ret >= buffer_size - offset) break;
        offset += ret;

        ret = snprintf(buffer + offset, buffer_size - offset,
            "gnb_network_receive_bytes_total{network=\"%s\",node=\"%"PRIu64"\"} %"PRIu64"\n",
            network, node->uuid64, node->out_bytes
        );
        if (ret < 0 || ret >= buffer_size - offset) break;
        offset += ret;

        ret = snprintf(buffer + offset, buffer_size - offset,
            "gnb_network_transmit_bytes_total{network=\"%s\",node=\"%"PRIu64"\"} %"PRIu64"\n",
            network, node->uuid64, node->in_bytes
        );
        if (ret < 0 || ret >= buffer_size - offset) break;
        offset += ret;

        if (GNB_NODE_STATUS_IPV4_PONG & node->udp_addr_status) {
            ret = snprintf(buffer + offset, buffer_size - offset,
                "gnb_node_addr4_ping_latency_ms{network=\"%s\",node=\"%"PRIu64"\"} %"PRIi64"\n",
                network, node->uuid64, node->addr4_ping_latency_usec / 1000
            );
            if (ret < 0 || ret >= buffer_size - offset) break;
            offset += ret;
        }

        if (GNB_NODE_STATUS_IPV6_PONG & node->udp_addr_status) {
            ret = snprintf(buffer + offset, buffer_size - offset,
                "gnb_node_addr6_ping_latency_ms{network=\"%s\",node=\"%"PRIu64"\"} %"PRIi64"\n",
                network, node->uuid64, node->addr6_ping_latency_usec / 1000
            );
            if (ret < 0 || ret >= buffer_size - offset) break;
            offset += ret;
        }
    }
}

// 生成 DOT 格式的节点连接图字符串
static void generate_dot_string(char *buffer, size_t buffer_size, gnb_ctl_block_t *ctl_block) {
    size_t offset = 0;
    int ret;
    gnb_node_t *nodes = ctl_block->node_zone->node;
    size_t node_num = ctl_block->node_zone->node_num;
    gnb_node_t *local_node = NULL;

    // 找到本地节点
    for (size_t i = 0; i < node_num; i++) {
        if (nodes[i].uuid64 == ctl_block->core_zone->local_uuid) {
            local_node = &nodes[i];
            break;
        }
    }
    if (!local_node) return;

    // DOT 图开始
    ret = snprintf(buffer + offset, buffer_size - offset, "digraph G {\n  rankdir=LR;\n  node [shape=ellipse];\n");
    if (ret < 0 || ret >= buffer_size - offset) return;
    offset += ret;

    // 声明所有节点
    for (size_t i = 0; i < node_num; i++) {
        ret = snprintf(buffer + offset, buffer_size - offset, "  \"%"PRIu64"\";\n", nodes[i].uuid64);
        if (ret < 0 || ret >= buffer_size - offset) return;
        offset += ret;
    }

    // 从本地节点的视角绘制连接
    for (size_t i = 0; i < node_num; i++) {
        gnb_node_t *dst_node = &nodes[i];
        if (dst_node->uuid64 == local_node->uuid64) continue;

        ret = 0;

        int is_ipv4_direct = (dst_node->udp_addr_status & GNB_NODE_STATUS_IPV4_PONG);
        int is_ipv6_direct = (dst_node->udp_addr_status & GNB_NODE_STATUS_IPV6_PONG);

        if (is_ipv4_direct || is_ipv6_direct) {
            // 绘制直接连接（实线）
            char label[16];
            if (is_ipv4_direct && is_ipv6_direct) {
                strcpy(label, "IPv4&IPv6");
            } else if (is_ipv4_direct) {
                strcpy(label, "IPv4");
            } else {
                strcpy(label, "IPv6");
            }
            ret = snprintf(buffer + offset, buffer_size - offset,
                "  \"%"PRIu64"\" -> \"%"PRIu64"\" [label=\"%s\"];\n",
                local_node->uuid64, dst_node->uuid64, label);
        } else {
            // 检查是否存在可用的 unified forwarding 节点
            int has_uf_node = 0;
            for (int j = 0; j < GNB_UNIFIED_FORWARDING_NODE_ARRAY_SIZE; j++) {
                if (dst_node->unified_forwarding_node_array[j].uuid64 != 0 &&
                    (ctl_block->status_zone->keep_alive_ts_sec - dst_node->unified_forwarding_node_array[j].last_ts_sec) < GNB_UNIFIED_FORWARDING_NODE_ARRAY_EXPIRED_SEC) {
                    has_uf_node = 1;
                    break;
                }
            }
            if (has_uf_node) {
                // 绘制 unified forwarding 连接（虚线）
                ret = snprintf(buffer + offset, buffer_size - offset,
                    "  \"%"PRIu64"\" -> \"%"PRIu64"\" [style=dashed];\n",
                    local_node->uuid64, dst_node->uuid64);
            }
        }
        if (ret < 0 || ret >= buffer_size - offset) return;
        offset += ret;
    }

    // DOT 图结束
    snprintf(buffer + offset, buffer_size - offset, "}\n");
}

static void send_http_response(int socket, const char *content_type, const char *body) {
    char http_response[BUFFER_SIZE * 2] = {0};
    snprintf(http_response, sizeof(http_response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        content_type,
        strlen(body),
        body
    );
    write(socket, http_response, strlen(http_response));
}

static void* thread_worker_func(void *data) {
    gnb_worker_t *gnb_worker = (gnb_worker_t *)data;
    exporter_worker_ctx_t *worker_ctx = (exporter_worker_ctx_t *)gnb_worker->ctx;
    gnb_core_t *gnb_core = worker_ctx->gnb_core;

    gnb_worker->thread_worker_flag = 1;
    gnb_worker->thread_worker_run_flag = 1;

    GNB_LOG1(gnb_core->log, GNB_LOG_ID_CORE, "Prometheus exporter listening on TCP port %d...\n", gnb_core->conf->exporter_port);

    while (gnb_worker->thread_worker_flag) {
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        int new_socket = accept(worker_ctx->server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            if (errno == EINTR || errno == EBADF) break;
            perror("Accept failed");
            continue;
        }

        char buffer[BUFFER_SIZE] = {0};
        read(new_socket, buffer, BUFFER_SIZE);

        if (strncmp(buffer, "GET /metrics", 12) == 0) {
            char metrics_string[BUFFER_SIZE] = {0};
            generate_metrics_string(metrics_string, sizeof(metrics_string), gnb_core->ctl_block);
            send_http_response(new_socket, "text/plain; version=0.0.4", metrics_string);
        } else if (strncmp(buffer, "GET /graph", 10) == 0) {
            char dot_string[BUFFER_SIZE * 2] = {0}; // 为DOT图提供更大缓冲区
            generate_dot_string(dot_string, sizeof(dot_string), gnb_core->ctl_block);
            send_http_response(new_socket, "text/vnd.graphviz", dot_string);
        }

        close(new_socket);
    }

    gnb_worker->thread_worker_run_flag = 0;
    return NULL;
}

static void init(gnb_worker_t *gnb_worker, void *ctx) {
    gnb_core_t *gnb_core = (gnb_core_t *)ctx;
    exporter_worker_ctx_t *worker_ctx = (exporter_worker_ctx_t *)gnb_heap_alloc(gnb_core->heap, sizeof(exporter_worker_ctx_t));
    memset(worker_ctx, 0, sizeof(exporter_worker_ctx_t));
    worker_ctx->gnb_core = gnb_core;
    worker_ctx->server_fd = -1;
    gnb_worker->ctx = worker_ctx;
    GNB_LOG1(gnb_core->log, GNB_LOG_ID_CORE, "%s init finish\n", gnb_worker->name);
}

static void release(gnb_worker_t *gnb_worker) {
    if (!gnb_worker || !gnb_worker->ctx) return;
    exporter_worker_ctx_t *worker_ctx = (exporter_worker_ctx_t *)gnb_worker->ctx;
    gnb_core_t *gnb_core = worker_ctx->gnb_core;
    gnb_heap_free(gnb_core->heap, worker_ctx);
    gnb_worker->ctx = NULL;
}

static int start(gnb_worker_t *gnb_worker) {
    exporter_worker_ctx_t *worker_ctx = (exporter_worker_ctx_t *)gnb_worker->ctx;
    gnb_core_t *gnb_core = worker_ctx->gnb_core;
    uint16_t listen_port = gnb_core->conf->exporter_port;

    if ((worker_ctx->server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Exporter socket creation failed");
        return -1;
    }

    int opt_reuse = 1;
    if (setsockopt(worker_ctx->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(opt_reuse))) {
        perror("Exporter setsockopt failed");
        close(worker_ctx->server_fd);
        return -1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(listen_port);

    if (bind(worker_ctx->server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Exporter bind failed");
        close(worker_ctx->server_fd);
        return -1;
    }

    if (listen(worker_ctx->server_fd, 10) < 0) {
        perror("Exporter listen failed");
        close(worker_ctx->server_fd);
        return -1;
    }

    pthread_create(&worker_ctx->thread_worker, NULL, thread_worker_func, gnb_worker);
    pthread_detach(worker_ctx->thread_worker);

    return 0;
}

static int stop(gnb_worker_t *gnb_worker) {
    exporter_worker_ctx_t *worker_ctx = (exporter_worker_ctx_t *)gnb_worker->ctx;
    if (worker_ctx) {
        gnb_worker->thread_worker_flag = 0;
        if (worker_ctx->server_fd != -1) {
            close(worker_ctx->server_fd);
            worker_ctx->server_fd = -1;
        }
    }
    return 0;
}

gnb_worker_t gnb_exporter_worker_mod = {
    .name = "gnb_exporter_worker",
    .init = init,
    .release = release,
    .start = start,
    .stop = stop,
    .notify = NULL,
    .ctx = NULL
};