/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef db_sub_source_hh
#define db_sub_source_hh

#include <string>
#include <vector>
#include <iterator>

#include <sqlite3.h>

#include "textview_curses.hh"
#include "hist_source.hh"

class db_label_source : public text_sub_source, public text_time_translator {
public:
    ~db_label_source() {
        this->clear();
    }

    bool has_log_time_column() const {
        return !this->dls_time_column.empty();
    };

    size_t text_line_count() {
        return this->dls_rows.size();
    };

    size_t text_size_for_line(textview_curses &tc, int line, line_flags_t flags) {
        return this->text_line_width(tc);
    };

    size_t text_line_width(textview_curses &curses) {
        size_t retval = 0;

        for (auto &dls_header : this->dls_headers) {
            retval += dls_header.hm_column_size + 1;
        }
        return retval;
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &label_out,
                             line_flags_t flags);

    void text_attrs_for_line(textview_curses &tc, int row, string_attrs_t &sa);

    void push_header(const std::string &colstr, int type, bool graphable);

    void push_column(const char *colstr);

    void clear();

    long column_name_to_index(const std::string &name) const;

    nonstd::optional<vis_line_t> row_for_time(struct timeval time_bucket);

    nonstd::optional<struct timeval> time_for_row(vis_line_t row) {
        if ((row < 0_vl) || (((size_t) row) >= this->dls_time_column.size())) {
            return nonstd::nullopt;
        }

        return this->dls_time_column[row];
    };

    struct header_meta {
        explicit header_meta(std::string name)
            : hm_name(std::move(name)),
              hm_column_type(SQLITE3_TEXT),
              hm_graphable(false),
              hm_log_time(false),
              hm_column_size(0) {

        };

        bool operator==(const std::string &name) const {
            return this->hm_name == name;
        };

        std::string hm_name;
        int hm_column_type;
        unsigned int hm_sub_type{0};
        bool hm_graphable;
        bool hm_log_time;
        size_t hm_column_size;
    };

    stacked_bar_chart<std::string> dls_chart;
    std::vector<header_meta> dls_headers;
    std::vector<std::vector<const char *>> dls_rows;
    std::vector<struct timeval> dls_time_column;
    std::vector<size_t> dls_cell_width;
    int dls_time_column_index{-1};

    static const char *NULL_STR;
};

class db_overlay_source : public list_overlay_source {
public:
    size_t list_overlay_count(const listview_curses &lv);

    bool list_value_for_overlay(const listview_curses &lv,
                                int y, int bottom,
                                vis_line_t row,
                                attr_line_t &value_out) override;

    bool dos_active{false};
    db_label_source *dos_labels{nullptr};
    std::vector<attr_line_t> dos_lines;
};
#endif
