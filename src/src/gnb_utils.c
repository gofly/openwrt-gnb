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
#include <ctype.h>
#ifdef __UNIX_LIKE_OS__
#include <arpa/inet.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "gnb_utils.h"

char * check_domain_name(char *host_string) {
    int i;
    bool has_alpha = false;

    if (!host_string || host_string[0] == '\0') {
        return NULL;
    }

    // A simple heuristic: if it contains any letter, we'll assume it's a domain.
    for (i = 0; host_string[i] != '\0'; i++) {
        if (isalpha(host_string[i])) {
            has_alpha = true;
            break;
        }
    }

    return has_alpha ? host_string : NULL;
}
