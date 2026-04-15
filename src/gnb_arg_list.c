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

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "gnb_arg_list.h"

gnb_arg_list_t *gnb_arg_list_init(int size){

    gnb_arg_list_t *arg_list = (gnb_arg_list_t *)malloc( sizeof(gnb_arg_list_t) + sizeof(char *) * size );
    
    arg_list->size = size;
    
    arg_list->argc = 0;
    
    return arg_list;
    
}


void gnb_arg_list_release(gnb_arg_list_t *arg_list){
    
    int i;
    
    if ( arg_list->argc > 0 ) {
        
        for ( i=0; i<arg_list->argc; i++ ) {
            free(arg_list->argv[i]);
        }
        
    }
    
    free(arg_list);

}


int gnb_arg_append(gnb_arg_list_t *arg_list,const char *arg){
    
    if ( arg_list->argc >= arg_list->size ) {
        return -1;
    }
    
    arg_list->argv[arg_list->argc] = strdup(arg);
    arg_list->argc++;
    
    return 0;
    
}


int gnb_arg_list_to_string(gnb_arg_list_t *arg_list, char *string, size_t string_len){

    int i;

    size_t argv_len;

    char *p = string;

    size_t copied_len = 0;

    for ( i=0; i < arg_list->argc; i++ ) {

        argv_len = strlen( arg_list->argv[i] );

        if ( (copied_len + argv_len) > string_len ) {
            break;
        }

        strncpy(p, arg_list->argv[i], string_len-copied_len);

        p += argv_len;

        *p = ' ';
        p += 1;

        copied_len += (argv_len+1);

    }

    if ( p == string ) {
        return -1;
    }

    *(p-1) = '\0';

    return 0;

}


#define SPACE_SEPARATOR         0
#define SINGLE_QUOTES_SEPARATOR 1
#define DOUBLE_QUOTES_SEPARATOR 2

#define ARG_SEPARATOR           0
#define ARG_STRING              1

gnb_arg_list_t *gnb_arg_string_to_list(char *string, int num){

    gnb_arg_list_t *arg_list = gnb_arg_list_init(num);
    char *p = string;
    char buffer[GNB_ARG_MAX_SIZE];
    int buffer_idx = 0;
    bool in_quotes = false;
    char quote_char = '\0';

    while (*p) {
        if (in_quotes) {
            if (*p == quote_char) {
                // 结束引号
                in_quotes = false;
                quote_char = '\0';
            } else {
                buffer[buffer_idx++] = *p;
            }
        } else {
            if (*p == '\'' || *p == '"') {
                // 开始引号
                in_quotes = true;
                quote_char = *p;
            } else if (*p == ' ') {
                // 遇到空格，一个参数结束
                if (buffer_idx > 0) {
                    buffer[buffer_idx] = '\0';
                    gnb_arg_append(arg_list, buffer);
                    buffer_idx = 0;
                }
            } else {
                // 普通字符
                buffer[buffer_idx++] = *p;
            }
        }

        if (buffer_idx >= GNB_ARG_MAX_SIZE - 1) {
            // 防止缓冲区溢出
            break;
        }
        p++;
    }

    // 处理最后一个参数
    if (buffer_idx > 0) {
        buffer[buffer_idx] = '\0';
        gnb_arg_append(arg_list, buffer);
    }

    return arg_list;
}
