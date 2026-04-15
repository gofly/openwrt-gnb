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

#ifndef GNB_HTTP_CLIENT_H
#define GNB_HTTP_CLIENT_H

#include <stddef.h>

/*
 * 通过HTTP GET请求获取内容
 * host: 主机IP地址
 * port: 端口
 * use_ssl: 1表示使用HTTPS, 0表示使用HTTP
 * path: 请求路径
 * response_buf: 用于存储响应内容的缓冲区
 * buf_size: 缓冲区大小
 * 返回值: 成功时返回响应内容的长度，失败时返回-1
 */
int gnb_http_get(const char *host, unsigned short port, int use_ssl, const char *path, char *response_buf, size_t buf_size);

#endif //GNB_HTTP_CLIENT_H