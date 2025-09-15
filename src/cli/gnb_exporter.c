#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>

#include "gnb_ctl_block.h"

#define BUFFER_SIZE 2048

#ifndef GNB_SKIP_BUILD_TIME
#define GNB_BUILD_STRING  "Build Time ["__DATE__","__TIME__"]"
#else
#define GNB_BUILD_STRING  "Build Time [Hidden]"
#endif

static int g_server_fd = -1;

void cleanup() {
    if (g_server_fd != -1) {
        printf("Closing server socket...\n");
        close(g_server_fd);
        g_server_fd = -1;
    }
}

static void show_useage(int argc, char *argv[]){
    printf("GNB Exporter version 1.6.0.a protocol version 1.6.0\n");
    printf("%s\n", GNB_BUILD_STRING);
    printf("Copyright (C) 2019 gnbdev\n");
    printf("Usage: %s -b CTL_BLOCK [OPTION]\n", argv[0]);
    printf("Command Summary:\n");
    printf("  -b, --ctl-block           ctl block mapper file\n");
    printf("  -p, --port                tcp port listen to\n");
    printf("      --help\n");
    printf("example:\n");
    printf("%s -b gnb.map -p 9100\n",argv[0]);

}

// Function to generate the Prometheus metrics string
void generate_metrics_string(char *buffer, size_t buffer_size, char *ctl_block_file) {
    size_t offset = 0;
    char *network;
    gnb_ctl_block_t *ctl_block;

    int ret;
    ctl_block = gnb_get_ctl_block(ctl_block_file, 0);
    if (NULL == ctl_block) {
        ret = snprintf(buffer + offset, buffer_size - offset,
            "open ctl block error [%s]\n", ctl_block_file
        );
        return;
    }

    network = ctl_block->core_zone->ifname;
    if (strncmp(network, "gnb-", 4) == 0) {
        network += 4;
    }

    uint8_t instance_up = 0; 
    if (time(NULL) - ctl_block->status_zone->keep_alive_ts_sec<5) {
        instance_up = 1;
    }

    ret = snprintf(buffer + offset, buffer_size - offset,
        "gnb_instance_up{network=\"%s\"} %"PRIu8"\n",
        network, instance_up
    );
    if (ret < 0 || ret >= buffer_size - offset) {
        gnb_mmap_release(ctl_block->mmap_block);
        return;
    }
    offset += ret;

    gnb_node_t *node;
    size_t node_num;
    node_num = ctl_block->node_zone->node_num;

    size_t i;

    for (i = 0; i < node_num; i++) {
        node = &ctl_block->node_zone->node[i];
        if (!((GNB_NODE_STATUS_IPV6_PONG | GNB_NODE_STATUS_IPV4_PONG) & node->udp_addr_status) ) continue;

        uint8_t state = 0;
        if (GNB_NODE_STATUS_IPV4_PONG & node->udp_addr_status) state|=1;
        if (GNB_NODE_STATUS_IPV6_PONG & node->udp_addr_status) state|=2;

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

        ret = snprintf(buffer + offset, buffer_size - offset,
            "gnb_node_addr4_ping_latency_ms{network=\"%s\",node=\"%"PRIu64"\"} %"PRIi64"\n",
            network, node->uuid64, node->addr4_ping_latency_usec/1000
        );
        if (ret < 0 || ret >= buffer_size - offset) break;
        offset += ret;

        ret = snprintf(buffer + offset, buffer_size - offset,
            "gnb_node_addr6_ping_latency_ms{network=\"%s\",node=\"%"PRIu64"\"} %"PRIi64"\n",
            network, node->uuid64, node->addr6_ping_latency_usec/1000
        );
        if (ret < 0 || ret >= buffer_size - offset) break;
        offset += ret;
    }
    
    gnb_mmap_release(ctl_block->mmap_block);
}

// Main function to run the HTTP server
int main(int argc, char *argv[]) {
    int new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    char *ctl_block_file = NULL;
    uint16_t listen_port = 9100;

    static struct option long_options[] = {

      { "ctl-block",            required_argument, 0, 'b' },
      { "port",                 required_argument, 0, 'p' },
      { "help",                 no_argument,       0, 'h' },
      { 0, 0, 0, 0 }
    };

    setvbuf(stdout,NULL,_IOLBF,0);
    
    atexit(cleanup);

    int opt;

    while (1) {

        int option_index = 0;

        opt = getopt_long (argc, argv, "b:p:",long_options, &option_index);

        if ( opt == -1 ) {
            break;
        }

        switch (opt) {
        case 'b':
            ctl_block_file = optarg;
            break;

        case 'p':
            errno = 0;
            char *endptr;
            unsigned long val;
            val = strtoul(optarg, &endptr, 10);
            if (errno != 0 || *endptr != '\0' || endptr == optarg || val > USHRT_MAX) {
                fprintf(stderr, "Invalid port number: '%s'\n", optarg);
                return EXIT_FAILURE;
            }
            listen_port = (uint16_t)val;
            break;
        case 'h':
            show_useage(argc, argv);
            exit(0);
        default:
            break;
        }
    }


    if ( NULL == ctl_block_file ) {
        show_useage(argc,argv);
        exit(0);
    }

    // Create a TCP socket
    if ((g_server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    int opt_reuse = 1;
    if (setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(opt_reuse))) {
        perror("setsockopt failed");
        close(g_server_fd);
        exit(EXIT_FAILURE);
    }

    // Set server address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(listen_port);

    // Bind the socket to the address and port
    if (bind(g_server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(g_server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Prometheus exporter listening on TCP port %d...\n", listen_port);

    while (1) {
        // Accept a new connection
        if ((new_socket = accept(g_server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if (setsockopt(new_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("setsockopt SO_RCVTIMEO failed");
            close(new_socket);
            continue;
        }

        if (setsockopt(new_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
            perror("setsockopt SO_SNDTIMEO failed");
            close(new_socket);
            continue;
        }

        // Read the incoming HTTP request
        read(new_socket, buffer, BUFFER_SIZE);

        // A simple check for GET /metrics
        if (strncmp(buffer, "GET /metrics", 12) == 0) {
            char metrics_string[BUFFER_SIZE] = {0};
            generate_metrics_string(metrics_string, sizeof(metrics_string), ctl_block_file);

            // Construct the HTTP response
            char http_response[BUFFER_SIZE * 2] = {0};
            snprintf(http_response, sizeof(http_response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain; version=0.0.4\r\n"
                "Content-Length: %zu\r\n"
                "\r\n"
                "%s",
                strlen(metrics_string),
                metrics_string
            );

            // Send the response
            write(new_socket, http_response, strlen(http_response));
        }

        // Close the connection
        close(new_socket);
    }

    return 0;
}