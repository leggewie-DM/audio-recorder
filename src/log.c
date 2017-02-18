/*
 * Copyright (c) Team audio-recorder.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License (GPL3), or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Library General Public License 3 for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License 3 along with this program; if not, see /usr/share/common-licenses/GPL file
 * or <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdarg.h>
#include <glib.h>
#include <gdk/gdk.h>
#include "log.h"

void log_message(const gchar *_file, const gint _line, const gchar *type, const gchar *msg_format, ...) {
    va_list args;
    va_start(args, msg_format);

    if (type)
        fputs(type, stderr);

    if (_file)
        fprintf(stderr, "%s, line %d: " , _file, _line);

    vfprintf(stderr, msg_format, args);
    va_end(args);

    fflush(stderr);
}

