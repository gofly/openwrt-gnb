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
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#define closesocket close
#endif
#include <sys/time.h>

#include "gnb_http_client.h"
#include "miniupnpc/connecthostport.h"
#include "miniupnpc/receivedata.h"

#if defined(WITH_MBEDTLS)
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/debug.h"
#else
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>
#endif

int gnb_http_get(const char *host, unsigned short port, int use_ssl, const char *path, char *response_buf, size_t buf_size) {
    SOCKET sock;
    char request[1024];
    int request_len;
    int recv_len;
    char *body_start;
    int body_len = -1;

#if defined(WITH_MBEDTLS)
    mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_config conf;
    int ret;
#else
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
#endif

    // 1. 连接到服务器
    sock = connecthostport(host, port, 0);
    if (ISINVALID(sock)) {
        return -1;
    }

    // 为所有连接（包括即将进行的SSL）设置一个5秒的接收超时
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        // 可以选择记录一个错误，但即使设置失败也继续尝试
        // perror("setsockopt failed");
    }


    if (use_ssl) {
#if defined(WITH_MBEDTLS)
        mbedtls_net_init(&server_fd);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_x509_crt_init(&cacert);
        mbedtls_entropy_init(&entropy);

        if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0) {
            goto mbedtls_cleanup;
        }

        if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
            goto mbedtls_cleanup;
        }

        // 加载 CA 证书，尝试系统默认路径
        // 在 Linux 上通常是 /etc/ssl/certs
        ret = mbedtls_x509_crt_parse_path(&cacert, "/etc/ssl/certs");
        if (ret < 0) {
            // 如果失败，可以尝试其他路径或记录错误
            // 在这里我们简单地继续，但验证可能会因为没有CA而失败
        }
        mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);

        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED); // 验证服务器证书
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

        if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
            goto mbedtls_cleanup;
        }

        if ((ret = mbedtls_ssl_set_hostname(&ssl, host)) != 0) {
            goto mbedtls_cleanup;
        }

        server_fd.fd = sock;
        mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                goto mbedtls_cleanup;
            }
        }
#else // Fallback to OpenSSL
        #if OPENSSL_VERSION_NUMBER < 0x10100000L
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        ctx = SSL_CTX_new(SSLv23_client_method());
        #else
        OPENSSL_init_ssl(0, NULL);
        ctx = SSL_CTX_new(TLS_client_method());
        #endif
        if (!ctx) {
            closesocket(sock);
            return -1;
        }

        // 设置默认的CA证书路径
        if (!SSL_CTX_set_default_verify_paths(ctx)) {
            // 处理错误
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, sock);

        if (SSL_connect(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            closesocket(sock);
            return -1;
        }
#endif
    }

    // 2. 构建HTTP(S) GET请求
    request_len = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Connection: close\r\n"
                           "User-Agent: gnb\r\n"
                           "\r\n",
                           path, host);

    // 3. 发送请求
    if (use_ssl) {
#if defined(WITH_MBEDTLS)
        ret = mbedtls_ssl_write(&ssl, (const unsigned char *)request, request_len);
        if (ret < 0) goto mbedtls_cleanup;
#else
        if (SSL_write(ssl, request, request_len) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            closesocket(sock);
            return -1;
        }
#endif
    } else {
        if (send(sock, request, request_len, 0) < 0) {
            closesocket(sock);
            return -1;
        }
    }

    // 4. 接收响应
    if (use_ssl) {
#if defined(WITH_MBEDTLS)
        recv_len = mbedtls_ssl_read(&ssl, (unsigned char *)response_buf, buf_size - 1);
#else
        recv_len = SSL_read(ssl, response_buf, buf_size - 1);
#endif
    } else {
        // 使用 receivedata 函数，它带有超时机制
        recv_len = receivedata(sock, response_buf, buf_size - 1, 5000, NULL);
    }

    // 5. 清理和关闭连接
    if (use_ssl) {
#if defined(WITH_MBEDTLS)
mbedtls_cleanup:
        mbedtls_ssl_close_notify(&ssl);
        mbedtls_net_free(&server_fd);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_x509_crt_free(&cacert);
#else
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
#endif
    }

    closesocket(sock);

    if (recv_len <= 0) {
        return -1;
    }
    response_buf[recv_len] = '\0';

    // 6. 解析HTTP响应，提取body
    body_start = strstr(response_buf, "\r\n\r\n");
    if (body_start) {
        body_start += 4; // 跳过 "\r\n\r\n"
        
        // 检查 "200 OK"
        if (strstr(response_buf, "HTTP/1.1 200 OK") == NULL && strstr(response_buf, "HTTP/1.0 200 OK") == NULL) {
             return -1; // 非成功响应
        }

        body_len = recv_len - (body_start - response_buf);

        // 将body内容移动到缓冲区开头
        if (body_len > 0) {
            memmove(response_buf, body_start, body_len);
        }
        response_buf[body_len] = '\0';

    } else {
        return -1; // 未找到HTTP body
    }

    return body_len;
}