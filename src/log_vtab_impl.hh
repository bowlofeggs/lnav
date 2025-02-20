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

#ifndef vtab_impl_hh
#define vtab_impl_hh

#include <sqlite3.h>

#include <map>
#include <string>
#include <vector>

#include "logfile_sub_source.hh"

class textview_curses;

enum {
    VT_COL_LINE_NUMBER,
    VT_COL_PARTITION,
    VT_COL_LOG_TIME,
    VT_COL_LOG_ACTUAL_TIME,
    VT_COL_IDLE_MSECS,
    VT_COL_LEVEL,
    VT_COL_MARK,
    VT_COL_LOG_COMMENT,
    VT_COL_LOG_TAGS,
    VT_COL_FILTERS,
    VT_COL_MAX
};

class logfile_sub_source;

struct log_cursor {
    vis_line_t lc_curr_line;
    int        lc_sub_index;
    vis_line_t lc_end_line;

    void update(unsigned char op, vis_line_t vl, bool exact = true);

    void set_eof() {
        this->lc_curr_line = this->lc_end_line = vis_line_t(0);
    };

    bool is_eof() const {
        return this->lc_curr_line >= this->lc_end_line;
    };
};

const std::string LOG_BODY = "log_body";
const std::string LOG_TIME = "log_time";

class log_vtab_impl {
public:
    struct vtab_column {
        vtab_column(const std::string name = "",
                    int type = SQLITE3_TEXT,
                    const std::string collator = "",
                    bool hidden = false,
                    const std::string comment = "",
                    unsigned int subtype = 0)
            : vc_name(name),
              vc_type(type),
              vc_collator(collator),
              vc_hidden(hidden),
              vc_comment(comment),
              vc_subtype(subtype) {
        };

        vtab_column& with_comment(const std::string comment) {
            this->vc_comment = comment;
            return *this;
        }

        std::string vc_name;
        int         vc_type;
        std::string vc_collator;
        bool vc_hidden;
        std::string vc_comment;
        int vc_subtype;
    };

    static std::pair<int, unsigned int> logline_value_to_sqlite_type(value_kind_t kind);

    log_vtab_impl(const intern_string_t name) : vi_supports_indexes(true), vi_name(name) {
        this->vi_attrs.resize(128);
    };
    virtual ~log_vtab_impl() = default;

    const intern_string_t get_name() const
    {
        return this->vi_name;
    };

    std::string get_table_statement();

    virtual bool is_valid(log_cursor &lc, logfile_sub_source &lss) {
        content_line_t    cl(lss.at(lc.lc_curr_line));
        std::shared_ptr<logfile> lf = lss.find(cl);
        auto lf_iter = lf->begin() + cl;

        if (!lf_iter->is_message()) {
            return false;
        }

        return true;
    };

    virtual bool next(log_cursor &lc, logfile_sub_source &lss) = 0;

    virtual void get_columns(std::vector<vtab_column> &cols) const { };

    virtual void get_foreign_keys(std::vector<std::string> &keys_inout) const
    {
        keys_inout.emplace_back("log_line");
        keys_inout.emplace_back("min(log_line)");
        keys_inout.emplace_back("log_mark");
        keys_inout.emplace_back("log_time_msecs");
    };

    virtual void extract(std::shared_ptr<logfile> lf,
                         uint64_t line_number,
                         shared_buffer_ref &line,
                         std::vector<logline_value> &values)
    {
        auto format = lf->get_format();

        this->vi_attrs.clear();
        format->annotate(line_number, line, this->vi_attrs, values, false);
    };

    bool vi_supports_indexes;
    int vi_column_count;
    string_attrs_t vi_attrs;
protected:
    const intern_string_t vi_name;
};

class log_format_vtab_impl : public log_vtab_impl {

public:
    log_format_vtab_impl(const log_format &format) :
            log_vtab_impl(format.get_name()), lfvi_format(format) {

    }

    virtual bool next(log_cursor &lc, logfile_sub_source &lss)
    {
        lc.lc_curr_line = lc.lc_curr_line + vis_line_t(1);
        lc.lc_sub_index = 0;

        if (lc.is_eof()) {
            return true;
        }

        auto cl = content_line_t(lss.at(lc.lc_curr_line));
        auto lf = lss.find(cl);
        auto lf_iter = lf->begin() + cl;
        uint8_t mod_id = lf_iter->get_module_id();

        if (!lf_iter->is_message()) {
            return false;
        }

        auto format = lf->get_format();
        if (format->get_name() == this->lfvi_format.get_name()) {
            return true;
        } else if (mod_id && mod_id == this->lfvi_format.lf_mod_index) {
            // XXX
            return true;
        }

        return false;
    };

protected:
    const log_format &lfvi_format;

};

typedef int (*sql_progress_callback_t)(const log_cursor &lc);
typedef void (*sql_progress_finished_callback_t)();

extern struct _log_vtab_data {
    sql_progress_callback_t lvd_progress;
    sql_progress_finished_callback_t lvd_finished;
    std::string lvd_source;
    int lvd_line_number{0};
} log_vtab_data;

class sql_progress_guard {
public:
    sql_progress_guard(sql_progress_callback_t cb,
                       sql_progress_finished_callback_t fcb,
                       const std::string &source,
                       int line_number) {
        log_vtab_data.lvd_progress = cb;
        log_vtab_data.lvd_finished = fcb;
        log_vtab_data.lvd_source = source;
        log_vtab_data.lvd_line_number = line_number;
    };

    ~sql_progress_guard() {
        if (log_vtab_data.lvd_finished) {
            log_vtab_data.lvd_finished();
        }
        log_vtab_data.lvd_progress = nullptr;
        log_vtab_data.lvd_finished = nullptr;
        log_vtab_data.lvd_source.clear();
        log_vtab_data.lvd_line_number = 0;
    };
};

class log_vtab_manager {
public:
    typedef std::map<intern_string_t, std::shared_ptr<log_vtab_impl>>::const_iterator iterator;

    log_vtab_manager(sqlite3 *db,
                     textview_curses &tc,
                     logfile_sub_source &lss);
    ~log_vtab_manager();

    textview_curses *get_view() const { return &this->vm_textview; };

    logfile_sub_source *get_source() { return &this->vm_source; };

    std::string register_vtab(std::shared_ptr<log_vtab_impl> vi);
    std::string unregister_vtab(intern_string_t name);

    std::shared_ptr<log_vtab_impl> lookup_impl(intern_string_t name) const
    {
        auto iter = this->vm_impls.find(name);

        if (iter != this->vm_impls.end()) {
            return iter->second;
        }
        return nullptr;
    };

    iterator begin() const
    {
        return this->vm_impls.begin();
    };

    iterator end() const
    {
        return this->vm_impls.end();
    };

private:
    sqlite3 *           vm_db;
    textview_curses &vm_textview;
    logfile_sub_source &vm_source;
    std::map<intern_string_t, std::shared_ptr<log_vtab_impl>> vm_impls;
};
#endif
