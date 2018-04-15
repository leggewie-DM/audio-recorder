/*
 * Copyright (c) 2011-2017 Osmo Antero.
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
#include "timer.h"
#include "log.h"
#include "support.h"
#include "utility.h"
#include <stdlib.h>

/* Parsing of timer commands:
 Syntax:
 action := comment | ("start" | "stop" | "pause") command

 comment := '#'...\n

 command := action_prep data

 action_prep := "after" | "at" | "if" | "on"

 data := time_notation | filesize | word

 time_notation := (##:##:## | #hour #min #sec) time_suffix

 time_suffix := "am" | "pm" | ""

 filesize := # ("bytes" | "kb"|"kib" | "mb"|"mib" | "gb"|"gib" | "tb"|"tib")

 word := ("silence" | "voice" | "sound" | "audio") time_notation signal_threshold

 signal_threshold := #dB | #[1.1, 100]% | #[0, 1.0]

 Note: # means an integer or floating point number.
 Comments begin with '#' and ends at newline \n.

 In simplified form:
 start | stop | pause (at|after|if|on) ##:##:## (am|pm|) | ### (bytes|kb|mb|gb|tb) | (silence|voice|sound|audio) ## seconds (##dB | ##% | ##)

 The words "voice", "sound" and "audio" has exactly same meaning!

 The file size units:
 https://wiki.ubuntu.com/UnitsPolicy

 Some examples:
 start at 10:10 pm
 stop after 20.5 min

 start if voice
 start if sound 5s 10%

 # this is a comment

 stop at 08:00:30 am
 stop after 6 min 20 sec
 stop after 5 MB
 stop if silence 7 s -20 db   # duration is 7 seconds, threshold is -20dB
 stop if silence 7 s 30%  # duration is 7 seconds, threshold is 30 (=0.3)
 stop if silence 7 s 0.3  # duration is 7 seconds, threshold is 0.3 (=30%)
 stop after silence | 5GB | 35 min

 pause if silence 10s 7%

 start if audio -19dB
 stop if silence 10 sec -18 db | 20 GB | 10 pm
 stop if silence 10 16

 Usage:
 Call parser_parse_actions(txt), where txt is the command text.
 GList *parser_parse_actions(gchar *txt);
 It will return a pointer to g_timer_list. This is a GList of TimerRec records.
*/

#define MAX_TOKEN_LEN 128

typedef enum {TOK_NONE, TOK_NUMERIC, TOK_TIME, TOK_TEXT} TokenType;

typedef struct {
    TokenType type;
    gchar tok[MAX_TOKEN_LEN+1];
} TokenRec;

typedef struct {
    gchar *buf;
    gchar *pos;
    gint len;
    gchar back_ch;
    guint line_no;
} ParserRec;

typedef struct {
    gchar *label;
    gchar *translation;
} LangRec;

LangRec g_transtable[] =  {
    // Example: start at 10:30 pm
    {"start",   NULL},

    // Example: stop after 200 kb
    {"stop",    NULL},

    // Start/stop/pause at, after, if, on
    // Example: start at 10:30 pm
    {"at",      NULL},

    // Example: stop after 1 hour 20 min
    {"after",   NULL},

    // Example: stop if 2GB
    {"if",      NULL},

    // Example: start on voice | 14:00 pm
    {"on",      NULL},

    // Start/pause on "voice".
    // Example: start on voice | 14:00 pm
    {"voice",   NULL},

    // Start/pause on "audio"
    // Example: start on audio | 14:00 pm
    {"audio",   NULL},

    // Start/pause on "sound"
    // Example: start on sound | 14:00 pm
    {"sound",   NULL},

    // Time: hour
    // Example: stop after 1 hour | 1 h
    {"hour",    NULL},

    // Time: hour
    // Example: stop after 1 hour | 1 h
    {"h",       NULL},

    // Time: minutes
    // Example: stop after 20 minutes | 20 min | 20 m
    {"minutes", NULL},

    // Time: minutes
    // Example: stop after 20 minutes | 20 min | 20 m
    {"min",     NULL},

    // Time: minutes
    // Example: stop after 20 minutes | 20 min | 20 m
    {"m",       NULL},

    // Time: seconds
    // Example: pause after 60 seconds | 60 sec | 60 s
    {"seconds", NULL},

    // Time: seconds
    // Example: pause after 60 seconds | 60 sec | 60 s
    {"sec",     NULL},

    // Time: seconds
    // Example: pause after 60 seconds | 60 sec | 60 s
    {"s",       NULL},

    // Example: pause if 2000 bytes | 2000 byte
    {"bytes",   NULL},

    // Example: pause if 2000 bytes | 2000 byte
    {"bytes",   NULL},

    // "|" or "or"
    // Example: pause if silence -20 dB | 2 GB or 10:20 pm
    {"or",      NULL},

    // Clock time; ante meridiem, before midday
    // Example: start at 09:00 am
    {"am",      NULL},

    // Clock time; post meridiem, after midday
    // Example: stop at 09:00 pm
    {"pm",      NULL},
};

// Global variables to this module
static ParserRec g_parser;
static TokenRec g_curtoken;
static TokenRec g_backtoken;
static GList *g_timer_list = NULL;

static void parser_init(gchar *txt);
static void parser_clear();

static void parser_free_node(TimerRec *tr);

static void parser_parse_action();
static void parser_parse_data();

static gboolean match_lang(gchar *tok, gchar *text);
static gboolean match_word(gchar *l_word, gchar *word, ...);

static LangRec *get_translated_str(gchar *label);

static gchar parser_get_ch();
static gchar parser_get_ch_ex();
static void parser_put_back_ch(gchar ch);

static void parser_get_token();

static TimerRec *parser_get_last();
static TimerRec *parser_add_action(gchar action);
static void parser_fix_list();

static void parser_print_error(gchar *msg);

static gchar *parser_type_to_str(gint type);

void parser_module_init() {
    LOG_DEBUG("Init timer-parser.c.\n");

    g_timer_list = NULL;
    parser_init(NULL);
}

void parser_module_exit() {
    LOG_DEBUG("Clean up timer-parser.c.\n");

    parser_clear();

    // Free list
    parser_free_list();
}

GList *parser_parse_actions(gchar *txt) {
    parser_clear();
    parser_init(txt);

    // 'S'tart... | S'T'op..., 'P'ause...
    parser_parse_action();

    parser_fix_list();

    // Return the list head
    return g_timer_list;
}

static void parser_init(gchar *txt) {
    // Init parser record
    if (txt) {
        g_parser.buf = g_utf8_strdown(txt, -1);
        g_parser.len = g_utf8_strlen(g_parser.buf, -1);
    } else {
        g_parser.buf = NULL;
        g_parser.len = 0;
    }

    g_parser.pos = g_parser.buf;
    g_parser.back_ch = '\0';

    g_parser.line_no = 1;

    *g_curtoken.tok = '\0';
    *g_backtoken.tok = '\0';

    // Free the existing TimerRec list
    parser_free_list();
}

static void parser_clear() {
    // Clear parser record
    if (g_parser.buf)
        g_free(g_parser.buf);

    parser_init(NULL);
}

static void parser_print_error(gchar *msg) {
    LOG_ERROR("Timer command, line %d: %s.\n", g_parser.line_no, msg);
}

static gboolean match_lang(gchar *tok, gchar *text) {
    // Check if the given token (tok) matches the text (in plain english or translated language)
    gboolean ret = !g_strcmp0(tok, text);
    if (ret) return TRUE;

    LangRec *lang_rec = get_translated_str(text);
    ret = FALSE;
    if (lang_rec) {
        ret = !g_strcmp0(tok, lang_rec->label);
        if (!ret)
            ret = !g_strcmp0(tok, lang_rec->translation);
    }
    return ret;
}

static gboolean match_word(gchar *l_word, gchar *word, ...) {
    // Check if l_word matches one of the given words (in plain english or translated language).
    va_list args;
    va_start(args, word);

    gboolean found = FALSE;

    gchar *text = word;
    while (text) {
        if (match_lang(l_word, text)) {
            found = TRUE;
            break;
        }
        text = va_arg(args, gchar *);
    }
    va_end(args);

    return found;
}

static void parser_put_back_ch(gchar ch) {
    // Put back a character
    ParserRec *p = &g_parser;
    p->back_ch = ch;
}

static gchar parser_get_ch() {
    // Get next character
    ParserRec *p = &g_parser;

    if (!p->buf) return '\0';

    // Check back buf
    gchar ch = '\0';
    if (p->back_ch != '\0') {
        ch = p->back_ch;
        p->back_ch = '\0';
        return ch;
    }

    gchar *end = g_utf8_offset_to_pointer(p->buf, p->len - 1);

    if (p->pos > end) return '\0';

    if (*p->pos == '\0') return '\0';

    if (*p->pos == '\n') {
        // Count lines
        p->line_no++;
    }

    // The char
    ch = *p->pos;

    // Advance head
    p->pos++;

    return ch;
}

static void parser_remove_space() {
    // Remove white spaces from the text
    gchar ch = parser_get_ch();
    while (g_unichar_isspace(ch) && ch != '\0') {
        ch = parser_get_ch();
    }
    parser_put_back_ch(ch);
}

static void parser_remove_comment() {
    // Remove characters until \n
    gchar ch = parser_get_ch();
    while (ch != '\0' && ch != '\n') {
        ch = parser_get_ch();
    }
}

static gchar parser_get_ch_ex() {
    // Get next char, remove spaces and comments

    // Remove spaces
    parser_remove_space();
    gchar ch = '\0';

LBL_1:
    ch = parser_get_ch();
    if (ch == '\0') return ch;

    if (g_unichar_isspace(ch)) {
        // Remove spaces
        parser_remove_space();
        goto LBL_1;
    } else if (ch == '#') {
        // Remove EOL comment
        parser_remove_comment();
        goto LBL_1;
    }

    return ch;
}

static TokenRec parser_get_token_ex() {
    // Get next token from the text.
    // Return token.
    TokenRec t;
    *t.tok = '\0';
    t.type = TOK_NONE;

    gchar ch = parser_get_ch_ex();
    if (ch == '\0') return t;

    // An integer or decimal number: +-0-9, 0-9.0-9
    // Ot time value of format ##:##:##
    gint i = 0;
    while ((g_unichar_isdigit(ch) || g_utf8_strchr(".:+-", 4, ch)) && ch != '\0') {
        if (i >= MAX_TOKEN_LEN) break;
        t.tok[i++] = ch;
        ch = parser_get_ch();
    }

    // Got a numeric token?
    if (i > 0) {
        // Put last ch back
        parser_put_back_ch(ch);

        // 0-terminate
        t.tok[i] = '\0';

        // Time notation hh:mm:ss?
        if (g_utf8_strchr(t.tok, g_utf8_strlen(t.tok, -1), ':'))
            t.type = TOK_TIME;
        else
            // Got numeric value
            t.type = TOK_NUMERIC;

        return t;
    }

    // Is it a "|"  (OR token)?
    if (ch == '|') {
        g_utf8_strncpy(t.tok, "|", 1);
        t.type = TOK_TEXT;
        return t;
    }
    // Is it a "%" token?
    else if (ch == '%') {
        g_utf8_strncpy(t.tok, "%", 1);
        t.type = TOK_TEXT;
        return t;
    }

    // Is it a letter, a character?
    // Read a string.
    i = 0;

    // Note: we do NOT use ">", "<", ">=", "<=", "=", "==" tokens here. We merely get rid of them.
    // Just in case user writes "stop if filezize >= 100 MB". This is illegal syntax but we make our best to interpret it like "stop if 100 MB".
    while ((g_unichar_isalpha(ch) || g_utf8_strchr("><=", -1, ch)) && ch != '\0') {
        if (i >= MAX_TOKEN_LEN) break;
        t.tok[i++] = ch;
        ch = parser_get_ch();
    }

    if (i > 0) {
        // Put last ch back
        parser_put_back_ch(ch);

        // 0-terminate
        t.tok[i] = '\0';
        t.type = TOK_TEXT;
        return t;
    }

    return t;
}

static void parser_put_token_back() {
    // Put token back
    g_backtoken = g_curtoken;
}

static void parser_get_token() {
    // Get next token. Check back buffer.

    if (*g_backtoken.tok) {
        g_curtoken = g_backtoken;
        *g_backtoken.tok = '\0';
        return;
    }

    // Set the current token
    g_curtoken = parser_get_token_ex();

    // puts(g_curtoken.tok);
}

static LangRec *get_translated_str(gchar *label) {
    // Get LangRec for the given text label
    guint siz = sizeof(g_transtable) / sizeof(g_transtable[0]);
    guint i = 0;
    for (i=0; i< siz; i++) {

        if (!g_strcmp0(label, g_transtable[i].label))
            return &g_transtable[i];

        if (!g_strcmp0(label, g_transtable[i].translation))
            return &g_transtable[i];
    }
    return NULL;
}

static gboolean starts_with(gchar *s, gchar *what) {
    // Test if string s starts with "what" (has that prefix)

    if (!(s && what)) return FALSE;

    const gchar *p = g_strstr_len(s, -1, what);
    // The "s" starts with "what", at 1.st byte.
    return (p == s);
}

static double tok_to_num(gchar *tok) {
    // Convert tok to double
    return atof(tok);
}

static void normalize_time(TimerRec *tr) {
    // Normalize decimal values to real hours/minutes/seconds
    // Eg. 2.5h means 2h 30minutes 0seconds

    // Convert to 24 hours clock
    if (!g_strcmp0(tr->label, "pm") && tr->val[0] <= 12) {
        tr->val[0] += 12.0;
    }

    if (tr->val[0] > 24.0) tr->val[0] = 24.0;

    gdouble secs = tr->val[0]*3600.0 + tr->val[1]*60.0 + tr->val[2];

    // Convert back to hour/min/sec
    tr->val[0] = (guint)(secs / 3600);
    secs = secs - (tr->val[0]*3600);

    tr->val[1] = (guint)(secs / 60);

    tr->val[2] = secs - (tr->val[1]*60);
}

static void parser_parse_data() {

    // Get the actual TimerRec record (last one)
    TimerRec *tr = parser_get_last();

    // Safety counter
    guint loop_count = 0;

    // State for some intricat values
    guint state = 0;

    gboolean seconds_set = FALSE;
    gboolean threshold_set = FALSE;

    while (1) {

        if (loop_count++ > 500) {
            // Insane loop
            break;
        }

        if (g_curtoken.type == TOK_NONE) {
            break;
        }

        gdouble val = 0.0;
        gint tok_type = -1;
        // Numeric values for clock time/duration, file size or threshold (in dB, % or plain value [0, 1.0])
        if (g_curtoken.type == TOK_NUMERIC) {

            // Default data type
            // tr->data_type = 't';

            tok_type = g_curtoken.type;

            val = tok_to_num(g_curtoken.tok);

            // Next token
            parser_get_token();
        }

        // Clock time of format hh:mm:ss
        else if (g_curtoken.type == TOK_TIME) {
            // hh
            // hh:mm
            // hh:mm:ss
            tr->data_type = 't';

            tok_type = g_curtoken.type;

            // Split the time string on ":"
            // eg. 10:20:30 (=10 hours, 20 minutes, 30 seconds)
            gchar **args = g_strsplit_set(g_curtoken.tok, ":", -1);

            guint i =0;
            for (i=0; args[i]; i++) {
                if (i < 3) {
                    tr->val[i] = tok_to_num(args[i]);
                }
            }

            // Free the string list
            g_strfreev(args);

            // Next token
            parser_get_token();

            state = 3;
        }

        // Token is string/text

        // am | pm
        // 11 am
        // 07 pm
        if (match_word(g_curtoken.tok, "am", NULL) ||
                match_word(g_curtoken.tok, "pm", NULL)) {

            g_utf8_strncpy(tr->label, g_curtoken.tok ,-1);

            if (tok_type == TOK_NUMERIC) {
                tr->val[0] = val;
            }
            tr->data_type = 't';

            state = 3;
        }
        // filesize: bytes | kb | mb | gb | tb
        // 123 mb
        // 14.5 gb
        else if (match_word(g_curtoken.tok, "bytes", "byte", NULL)) {
            tr->data_type = 'f';
            tr->val[0] = val;
            g_utf8_strncpy(tr->label, "bytes" , -1);
        } else if (starts_with(g_curtoken.tok, "kb") || starts_with(g_curtoken.tok, "kib")) {
            tr->data_type = 'f';
            tr->val[0] = val * 1E3;
            g_utf8_strncpy(tr->label, "kb" , -1);
        } else if (starts_with(g_curtoken.tok, "mb") || starts_with(g_curtoken.tok, "mib")) {
            tr->data_type = 'f';
            tr->val[0] = val * 1E6;
            g_utf8_strncpy(tr->label, "mb" , -1);
        } else if (starts_with(g_curtoken.tok, "gb") || starts_with(g_curtoken.tok, "gib")) {
            tr->data_type = 'f';
            tr->val[0] = val * 1E9;
            g_utf8_strncpy(tr->label, "gb" , -1);
        } else if (starts_with(g_curtoken.tok, "tb") || starts_with(g_curtoken.tok, "tib")) {
            tr->data_type = 'f';
            tr->val[0] = val * 1E12;
            g_utf8_strncpy(tr->label, "tb" , -1);
        }

        // hours, minutes, seconds
        else if (match_word(g_curtoken.tok, "h", NULL) || starts_with(g_curtoken.tok, "ho")) { // h, hour, hours, horas
            // 'd' = time duration
            tr->data_type = 'd';
            tr->val[0] = val;
            state = 3;
        } else if (match_word(g_curtoken.tok, "m", NULL) || starts_with(g_curtoken.tok, "mi")) { // m, min, minute, minutes
            // 'd' = time duration
            tr->data_type = 'd';
            tr->val[1] = val;
            state = 3;
        } else if (match_word(g_curtoken.tok, "s", NULL) || starts_with(g_curtoken.tok, "se")) { // s, sec, second, seconds

            if (str_length0(tr->threshold_unit) < 1) { /* == '\0'*/
                // Save threshold value (eg. 0.4)
                // "stop if silence 0.4 5s" => "stop if silence 5s 0.4"
                tr->threshold = tr->val[2];
            }

            // 'd' = time duration
            tr->data_type = 'd';
            tr->val[2] = val;

            state = 3;
        }

        // "silence"
        else if (match_word(g_curtoken.tok, "silence", NULL)) {
            tr->data_type = 'x';
            g_utf8_strncpy(tr->label, "silence" , -1);
            state = 2;
        }
        // "voice" | "audio" | "sound"
        else if (match_word(g_curtoken.tok, "voice", NULL)) {
            tr->data_type = 'x';
            g_utf8_strncpy(tr->label, "voice" , -1);
            state = 2;
        } else if (match_word(g_curtoken.tok, "audio", NULL)) {
            tr->data_type = 'x';
            g_utf8_strncpy(tr->label, "audio" , -1);
            state = 2;
        } else if (match_word(g_curtoken.tok, "sound", NULL)) {
            tr->data_type = 'x';
            g_utf8_strncpy(tr->label, "sound" , -1);
            state = 2;
        }

        // threshold dB (-100dB - -5dB)
        else if (match_word(g_curtoken.tok, "db", "decibel", NULL) || starts_with(g_curtoken.tok, "decib")) {
            // TODO: Check if "silence", "voice" | "audio" | "sound" token detected
            g_utf8_strncpy(tr->threshold_unit, "db", -1);
            tr->threshold = val;
        }

        // Threshold % (0 - 100%)
        else if (match_word(g_curtoken.tok, "%", NULL)) {
            g_utf8_strncpy(tr->threshold_unit, "%", -1);
            tr->threshold = val;
        }

        // "start", "stop", "pause"
        else if (match_word(g_curtoken.tok, "start", "stop", "pause", "|", NULL)) {

            // Put token back
            parser_put_token_back();

            // Return from this loop
            break;
        } else {

            if (state == 2)  {

                // silence|voice 10 0.4  (take 10)
                if (tok_type == TOK_NUMERIC) {
                    tr->val[2] = val; // duration 10s
                }
                state = 3;

                seconds_set = TRUE;

                // Put token back
                parser_put_token_back();

            } else if (state == 3) {

                // silence|voice 7 30  # take 30 (=30%)
                // silence|voice 7 0.3  # take 0.3 (=30%)
                if (tok_type == TOK_NUMERIC) {
                    tr->threshold = val;
                    if (tr->threshold > 1.0) {
                        // Value between [1%, 100%], assume it's a %-value
                        g_utf8_strncpy(tr->threshold_unit, "%", -1);
                    } else {
                        // Value between [0, 1.0] is a plain number
                        *(tr->threshold_unit) = '\0';
                    }

                }
                state = 0;

                threshold_set = TRUE;

                // Put token back
                parser_put_token_back();
            } else if (tok_type == TOK_NUMERIC) {

                if (val != 0.0) {
                    // Set default
                    tr->data_type = 't';
                    tr->val[0] = val;
                }

                // Put token back
                parser_put_token_back();

                break;
            }

        }

        // Next token
        parser_get_token();

    } // while...

    if (!g_strcmp0(tr->label, "silence") ||
            !g_strcmp0(tr->label, "voice") ||
            !g_strcmp0(tr->label, "sound") ||
            !g_strcmp0(tr->label, "audio")) {

        tr->data_type = 'x';
    }

    // We got time value?
    if (tr->data_type == 't' || tr->data_type == 'd') {
        // Normalize hours/minutes/seconds. Eg. 2.5 h becomes 2 h 30 minutes.
        normalize_time(tr);
    }

    // start if voice 0.5 --> start if voice 0 sec 0.5  (0.5 is a threshold level, not seconds!)
    if (seconds_set == TRUE && threshold_set == FALSE && tr->val[2] <= 1.00) {
        tr->threshold = tr->val[2];
        tr->val[2] = 0.0;
    }

}

static void parse_parse_line() {
    // start | stop | pause at|after|if|on 10:10:12 am/pm | 100 bytes/kb/mb/gb/tb | silence/voice/sound

    // Get next token
    parser_get_token();

    // Get last TimerRec
    TimerRec *tr = parser_get_last();

    // Remove action preposition; "at" | "after" | "if" | "on"
    gboolean got_action_prep = FALSE;

    if (match_word(g_curtoken.tok, "at", NULL)) {
        got_action_prep = TRUE;
    } else if (match_word(g_curtoken.tok, "after", NULL)) {
        tr->action_prep = 'a'; // 'a' = after
        got_action_prep = TRUE;
    } else if (match_word(g_curtoken.tok, "if", NULL)) {
        got_action_prep = TRUE;
    } else if (match_word(g_curtoken.tok, "on", NULL)) {
        got_action_prep = TRUE;
    }

    // Consumed token?
    if (got_action_prep) {
        // Get next token
        parser_get_token();
    }

    while (1) {

        // Parse data
        parser_parse_data();

        // Next token
        parser_get_token();

        // It is "|"  or "or"?
        if (!g_strcmp0(g_curtoken.tok, "|") || match_word(g_curtoken.tok, "or", NULL)) {

            // Add new record
            parser_add_action('X'); // 'X' = unknown at the moment

            // Next token
            parser_get_token();

        } else {

            // Put back
            parser_put_token_back();
            return;
        }
    }
}

static void parser_parse_action() {

    // Get first token
    parser_get_token();

    while (g_curtoken.type != TOK_NONE) {

        if (match_word(g_curtoken.tok, "start", NULL)) {
            // "start ..."
            parser_add_action('S'); // 'S' = start recording

            // Parse rest of the "start ..." line
            parse_parse_line();
        } else if (match_word(g_curtoken.tok, "stop", NULL)) {
            // "stop ..."
            parser_add_action('T'); // 'T' = sTop recording

            // Parse rest of the "stop ..." line
            parse_parse_line();
        } else if (match_word(g_curtoken.tok, "pause", NULL)) {
            // "pause ..."
            parser_add_action('P'); // 'P' = Pause recording

            // Parse rest of the "pause ..." line
            parse_parse_line();
        }

        else {
            // Unknown token
            gchar *msg = g_strdup_printf("Unknown token: %s.\n", g_curtoken.tok);
            parser_print_error(msg);
            g_free(msg);
        }

        // Get next token
        parser_get_token();
    }
}

static gchar *parser_type_to_str(gint type) {
    // Convert type to text (for debugging)
    switch (type) {
    case TOK_NUMERIC:
        return "TOK_NUMERIC";
    case TOK_TIME:
        return "TOK_TIME";
    case TOK_TEXT:
        return "TOK_TEXT";
    default:
        return "UNKNOWN TOKEN";
    }
}


// ==========================================

static void parser_free_node(TimerRec *tr) {
    // Free TimerRec node
    if (tr) g_free(tr);
}

static TimerRec *timer_new_rec(gchar action) {
    // Create new TimerRec node
    TimerRec *tr = g_malloc0(sizeof(TimerRec));
    tr->action = action;
    tr->day_of_year = -1;
    return tr;
}

static TimerRec *parser_add_action(gchar action) {
    // Add new TimerRec node to g_timer_list
    TimerRec *tr = tr = timer_new_rec(action);
    g_timer_list = g_list_append(g_timer_list, tr);
    return tr;
}

static TimerRec *parser_get_last() {
    // Return last TimerRec node from g_timer_list
    TimerRec *tr = NULL;
    if (!g_timer_list) {
        tr = timer_new_rec('X'); // X = Unknown action
        g_timer_list = g_list_append(g_timer_list, tr);
    }

    GList *last =  g_list_last(g_timer_list);
    return (TimerRec*)last->data;
}

static void parser_fix_list() {
    // Fix OR'ed commands.
    // Eg. "stop if 100 MB | 1 h 20 min | silence" becomes three separate nodes.
    // It becomes:
    // 	   "stop if 100 MB"
    //     "stop if 1 h 20 min"
    //     "stop if silence"
    //
    // Replace 'X' with previous node's action char.
    // Replace action_prep with previous node's action_prep.
    GList *item = g_list_first(g_timer_list);
    gchar last_action = '\0';
    gchar last_prep = '\0';
    while (item) {
        TimerRec *tr = (TimerRec*)item->data;

        if (last_action != '\0' && tr->action == 'X') {
            tr->action = last_action;
        }

        if (last_action != tr->action) {
            last_prep = '\0';
        }

        last_action = tr->action;

        if (last_prep != '\0' && tr->action_prep == '\0') {
            tr->action_prep = last_prep;
        }
        last_prep = tr->action_prep;

        item = g_list_next(item);
    }
}

const gchar *parser_get_action_name(gchar action) {
    switch (action) {
    case 'S':
        return "Start recording";

    case 'c':
    case 'C':
        return "Continue recording";

    case 'T':
        return "Stop recording";

    case 'p':
    case 'P':
        return "Pause recording";

    default:
        return "Unknown timer command";
    }
}

void parser_print_rec(TimerRec *tr) {
    gchar *action_str = NULL;
    switch (tr->action) {
    case 'S':
        action_str = "Start";
        break;

    case 'T':
        action_str = "sTop";
        break;

    case 'P':
        action_str = "Pause";
        break;
    }

    LOG_MSG("action:%c (%s)\n", tr->action, action_str);

    // Commented out:
    // LOG_MSG("\taction preposition (if/after/on/etc.):%c\n", tr->action_prep);

    // Is it silence/voice/sound/audio command?
    if (!g_strcmp0(tr->label, "silence") ||
            !g_strcmp0(tr->label, "voice") ||
            !g_strcmp0(tr->label, "sound") ||
            !g_strcmp0(tr->label, "audio")) {

        LOG_MSG("\tlabel: %s, delay:%3.1f %3.1f %3.1f threshold:%3.3f %s\n", tr->label, tr->val[0], tr->val[1], tr->val[2],
                tr->threshold, tr->threshold_unit);
    }

    switch (tr->data_type) {
    case 'd':
        LOG_MSG("\t%c, time duration: %3.1f %3.1f %3.1f\n", tr->data_type, tr->val[0], tr->val[1], tr->val[2]);
        break;

    case 't':
        LOG_MSG("\t%c, clock time: %3.1f %3.1f %3.1f\n", tr->data_type, tr->val[0], tr->val[1], tr->val[2]);
        break;

    case 'f':
        LOG_MSG("\t%c, filesize: %3.1f  (from %s)\n", tr->data_type, tr->val[0], tr->label);
        break;

    case 'x':
        ;
        break;

    default:
        LOG_MSG("\tUnknown data type in timer command.\n");
    }

}

void parser_print_list(GList *list) {
    LOG_MSG("---------------------------\n");
    g_list_foreach(list, (GFunc)parser_print_rec, NULL);
}

void parser_free_list() {
    // Free the TimerRec list
    g_list_foreach(g_timer_list, (GFunc)parser_free_node, NULL);
    g_list_free(g_timer_list);
    g_timer_list = NULL;
}

