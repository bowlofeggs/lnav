/**
 * Copyright (c) 2015, Timothy Stack
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

#include "config.h"

#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "lnav.hh"
#include "base/injector.bind.hh"
#include "base/lnav_log.hh"
#include "sql_util.hh"
#include "views_vtab.hh"
#include "view_curses.hh"

using namespace std;

template<>
struct from_sqlite<lnav_view_t> {
    inline lnav_view_t operator()(int argc, sqlite3_value **val, int argi) {
        const char *view_name = (const char *) sqlite3_value_text(val[argi]);
        auto view_index_opt = view_from_string(view_name);

        if (!view_index_opt) {
            throw from_sqlite_conversion_error("lnav view name", argi);
        }

        return view_index_opt.value();
    }
};

template<>
struct from_sqlite<text_filter::type_t> {
    inline text_filter::type_t operator()(int argc, sqlite3_value **val, int argi) {
        const char *type_name = (const char *) sqlite3_value_text(val[argi]);

        if (strcasecmp(type_name, "in") == 0) {
            return text_filter::INCLUDE;
        } else if (strcasecmp(type_name, "out") == 0) {
            return text_filter::EXCLUDE;
        }

        throw from_sqlite_conversion_error("filter type", argi);
    }
};

template<>
struct from_sqlite<pair<string, auto_mem<pcre>>> {
    inline pair<string, auto_mem<pcre>> operator()(int argc, sqlite3_value **val, int argi) {
        const char *pattern = (const char *) sqlite3_value_text(val[argi]);
        const char *errptr;
        auto_mem<pcre> code;
        int eoff;

        if (pattern == nullptr || pattern[0] == '\0') {
            throw from_sqlite_conversion_error("non-empty pattern", argi);
        }

        code = pcre_compile(pattern,
                            PCRE_CASELESS,
                            &errptr,
                            &eoff,
                            nullptr);

        if (code == nullptr) {
            throw sqlite_func_error(
                "Invalid regular expression in column {}: {} at offset {}",
                argi, errptr, eoff);
        }

        return make_pair(string(pattern), std::move(code));
    }
};

struct lnav_views : public tvt_iterator_cursor<lnav_views> {
    static constexpr const char *NAME = "lnav_views";
    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's views through this table.
CREATE TABLE lnav_views (
    name TEXT PRIMARY KEY,  -- The name of the view.
    top INTEGER,            -- The number of the line at the top of the view, starting from zero.
    left INTEGER,           -- The left position of the viewport.
    height INTEGER,         -- The height of the viewport.
    inner_height INTEGER,   -- The number of lines in the view.
    top_time DATETIME,      -- The time of the top line in the view, if the content is time-based.
    top_file TEXT,          -- The file the top line is from.
    paused INTEGER,         -- Indicates if the view is paused and will not load new data.
    search TEXT,            -- The text to search for in the view.
    filtering INTEGER       -- Indicates if the view is applying filters.
);
)";

    using iterator = textview_curses *;

    iterator begin() {
        return std::begin(lnav_data.ld_views);
    }

    iterator end() {
        return std::end(lnav_data.ld_views);
    }

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        lnav_view_t view_index = (lnav_view_t) distance(std::begin(lnav_data.ld_views), vc.iter);
        textview_curses &tc = *vc.iter;
        unsigned long width;
        vis_line_t height;

        tc.get_dimensions(height, width);
        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[view_index], -1,
                                    SQLITE_STATIC);
                break;
            case 1:
                sqlite3_result_int(ctx, (int) tc.get_top());
                break;
            case 2:
                sqlite3_result_int(ctx, tc.get_left());
                break;
            case 3:
                sqlite3_result_int(ctx, height);
                break;
            case 4:
                sqlite3_result_int(ctx, tc.get_inner_height());
                break;
            case 5: {
                auto *time_source = dynamic_cast<text_time_translator *>(tc.get_sub_source());

                if (time_source != nullptr && tc.get_inner_height() > 0) {
                    auto top_time_opt = time_source->time_for_row(tc.get_top());

                    if (top_time_opt) {
                        char timestamp[64];

                        sql_strftime(timestamp, sizeof(timestamp), top_time_opt.value(), 'T');
                        sqlite3_result_text(ctx, timestamp, -1, SQLITE_TRANSIENT);
                    } else {
                        sqlite3_result_null(ctx);
                    }
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 6: {
                to_sqlite(ctx, tc.map_top_row([](const auto& al) {
                    return get_string_attr(al.get_attrs(), &logline::L_FILE) | [](const auto* sa) {
                        auto lf = (logfile *) sa->sa_value.sav_ptr;

                        return nonstd::make_optional(lf->get_filename());
                    };
                }));
                break;
            }
            case 7:
                sqlite3_result_int(ctx, tc.is_paused());
                break;
            case 8:
                to_sqlite(ctx, tc.get_current_search());
                break;
            case 9: {
                auto tss = tc.get_sub_source();

                if (tss != nullptr && tss->tss_supports_filtering) {
                    sqlite3_result_int(ctx, tss->tss_apply_filters);
                } else {
                    sqlite3_result_int(ctx, 0);
                }
                break;
            }
        }

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be deleted from the lnav_views table");
        return SQLITE_ERROR;
    }

    int insert_row(sqlite3_vtab *tab, sqlite3_int64 &rowid_out) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be inserted into the lnav_views table");
        return SQLITE_ERROR;
    };

    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &index,
                   const char *name,
                   int64_t top_row,
                   int64_t left,
                   int64_t height,
                   int64_t inner_height,
                   const char *top_time,
                   const char *top_file,
                   bool is_paused,
                   const char *search,
                   bool do_filtering) {
        textview_curses &tc = lnav_data.ld_views[index];
        text_time_translator *time_source = dynamic_cast<text_time_translator *>(tc.get_sub_source());

        if (tc.get_top() != top_row) {
            tc.set_top(vis_line_t(top_row));
        } else if (top_time != nullptr && time_source != nullptr) {
            date_time_scanner dts;
            struct timeval tv;

            if (dts.convert_to_timeval(top_time, -1, nullptr, tv)) {
                auto last_time_opt = time_source->time_for_row(tc.get_top());

                if (last_time_opt) {
                    auto last_time = last_time_opt.value();
                    if (tv != last_time) {
                        time_source->row_for_time(tv) | [&tc](auto row) {
                            tc.set_top(row);
                        };
                    }
                }
            } else {
                tab->zErrMsg = sqlite3_mprintf("Invalid time: %s", top_time);
                return SQLITE_ERROR;
            }
        }
        tc.set_left(left);
        tc.set_paused(is_paused);
        tc.execute_search(search);
        auto tss = tc.get_sub_source();
        if (tss != nullptr &&
            tss->tss_supports_filtering &&
            tss->tss_apply_filters != do_filtering) {
            tss->tss_apply_filters = do_filtering;
            tss->text_filters_changed();
        }

        return SQLITE_OK;
    };
};

struct lnav_view_stack : public tvt_iterator_cursor<lnav_view_stack> {
    using iterator = vector<textview_curses *>::iterator;

    static constexpr const char *NAME = "lnav_view_stack";
    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's view stack through this table.
CREATE TABLE lnav_view_stack (
    name TEXT
);
)";

    iterator begin() {
        return lnav_data.ld_view_stack.begin();
    }

    iterator end() {
        return lnav_data.ld_view_stack.end();
    }

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        textview_curses *tc = *vc.iter;
        auto view = lnav_view_t(tc - lnav_data.ld_views);

        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[view], -1,
                                    SQLITE_STATIC);
                break;
        }

        return SQLITE_OK;
    };

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        if ((size_t)rowid != lnav_data.ld_view_stack.size() - 1) {
            tab->zErrMsg = sqlite3_mprintf(
                "Only the top view in the stack can be deleted");
            return SQLITE_ERROR;
        }

        lnav_data.ld_last_view = *lnav_data.ld_view_stack.top();
        lnav_data.ld_view_stack.pop_back();
        return SQLITE_OK;
    };

    int insert_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid_out,
                   lnav_view_t view_index) {
        textview_curses *tc = &lnav_data.ld_views[view_index];

        ensure_view(tc);
        rowid_out = lnav_data.ld_view_stack.size() - 1;

        return SQLITE_OK;
    };

    int update_row(sqlite3_vtab *tab, sqlite3_int64 &index) {
        tab->zErrMsg = sqlite3_mprintf(
            "The lnav_view_stack table cannot be updated");
        return SQLITE_ERROR;
    };
};

struct lnav_view_filter_base {
    struct iterator {
        using difference_type = int;
        using value_type = text_filter;
        using pointer = text_filter *;
        using reference = text_filter &;
        using iterator_category = forward_iterator_tag;

        lnav_view_t i_view_index;
        int i_filter_index;

        iterator(lnav_view_t view = LNV_LOG, int filter = -1)
            : i_view_index(view), i_filter_index(filter) {
        }

        iterator &operator++() {
            while (this->i_view_index < LNV__MAX) {
                textview_curses &tc = lnav_data.ld_views[this->i_view_index];
                text_sub_source *tss = tc.get_sub_source();

                if (tss == nullptr) {
                    this->i_view_index = lnav_view_t(this->i_view_index + 1);
                    continue;
                }

                filter_stack &fs = tss->get_filters();

                this->i_filter_index += 1;
                if (this->i_filter_index >= (ssize_t) fs.size()) {
                    this->i_filter_index = -1;
                    this->i_view_index = lnav_view_t(this->i_view_index + 1);
                } else {
                    break;
                }
            }

            return *this;
        }

        bool operator==(const iterator &other) const {
            return this->i_view_index == other.i_view_index &&
                   this->i_filter_index == other.i_filter_index;
        }

        bool operator!=(const iterator &other) const {
            return !(*this == other);
        }
    };

    iterator begin() {
        iterator retval = iterator();

        return ++retval;
    }

    iterator end() {
        return {LNV__MAX, -1};
    }

    sqlite_int64 get_rowid(iterator iter) {
        textview_curses &tc = lnav_data.ld_views[iter.i_view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto &tf = *(fs.begin() + iter.i_filter_index);

        sqlite_int64 retval = iter.i_view_index;

        retval = retval << 32;
        retval = retval | tf->get_index();

        return retval;
    }
};

struct lnav_view_filters : public tvt_iterator_cursor<lnav_view_filters>,
    public lnav_view_filter_base {
    static constexpr const char *NAME = "lnav_view_filters";
    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's filters through this table.
CREATE TABLE lnav_view_filters (
    view_name TEXT,                   -- The name of the view.
    filter_id INTEGER DEFAULT 0,      -- The filter identifier.
    enabled   INTEGER DEFAULT 1,      -- Indicates if the filter is enabled/disabled.
    type      TEXT    DEFAULT 'out',  -- The type of filter (i.e. in/out).
    pattern   TEXT                    -- The filter pattern.
);
)";

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        textview_curses &tc = lnav_data.ld_views[vc.iter.i_view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto tf = *(fs.begin() + vc.iter.i_filter_index);

        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[vc.iter.i_view_index], -1,
                                    SQLITE_STATIC);
                break;
            case 1:
                to_sqlite(ctx, tf->get_index());
                break;
            case 2:
                sqlite3_result_int(ctx, tf->is_enabled());
                break;
            case 3:
                switch (tf->get_type()) {
                    case text_filter::INCLUDE:
                        sqlite3_result_text(ctx, "in", 2, SQLITE_STATIC);
                        break;
                    case text_filter::EXCLUDE:
                        sqlite3_result_text(ctx, "out", 3, SQLITE_STATIC);
                        break;
                    default:
                        ensure(0);
                }
                break;
            case 4:
                sqlite3_result_text(ctx,
                                    tf->get_id().c_str(),
                                    -1,
                                    SQLITE_TRANSIENT);
                break;
        }

        return SQLITE_OK;
    }

    int insert_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid_out,
                   lnav_view_t view_index,
                   nonstd::optional<int64_t> _filter_id,
                   nonstd::optional<bool> enabled,
                   nonstd::optional<text_filter::type_t> type,
                   pair<string, auto_mem<pcre>> pattern) {
        textview_curses &tc = lnav_data.ld_views[view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto filter_index = fs.next_index();
        if (!filter_index) {
            throw sqlite_func_error("Too many filters");
        }
        auto pf = make_shared<pcre_filter>(
            type.value_or(text_filter::type_t::EXCLUDE),
            pattern.first,
            *filter_index,
            pattern.second.release());
        fs.add_filter(pf);
        if (!enabled.value_or(true)) {
            pf->disable();
        }
        tss->text_filters_changed();
        tc.set_needs_update();

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        auto view_index = lnav_view_t(rowid >> 32);
        size_t filter_index = rowid & 0xffffffffLL;
        textview_curses &tc = lnav_data.ld_views[view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();

        for (const auto &iter : fs) {
            if (iter->get_index() == filter_index) {
                fs.delete_filter(iter->get_id());
                tss->text_filters_changed();
                break;
            }
        }
        tc.set_needs_update();

        return SQLITE_OK;
    }

    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid,
                   lnav_view_t new_view_index,
                   int64_t new_filter_id,
                   bool enabled,
                   text_filter::type_t type,
                   pair<string, auto_mem<pcre>> pattern) {
        auto view_index = lnav_view_t(rowid >> 32);
        auto filter_index = rowid & 0xffffffffLL;
        textview_curses &tc = lnav_data.ld_views[view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto iter = fs.begin();
        for (; iter != fs.end(); ++iter) {
            if ((*iter)->get_index() == (size_t) filter_index) {
                break;
            }
        }

        shared_ptr<text_filter> tf = *iter;

        if (new_view_index != view_index) {
            tab->zErrMsg = sqlite3_mprintf(
                "The view for a filter cannot be changed");
            return SQLITE_ERROR;
        }

        tf->lf_deleted = true;
        tss->text_filters_changed();

        auto pf = make_shared<pcre_filter>(type,
                                           pattern.first,
                                           tf->get_index(),
                                           pattern.second.release());

        if (!enabled) {
            pf->disable();
        }

        *iter = pf;
        tss->text_filters_changed();
        tc.set_needs_update();

        return SQLITE_OK;
    };
};

struct lnav_view_filter_stats : public tvt_iterator_cursor<lnav_view_filter_stats>,
    public lnav_view_filter_base {
    static constexpr const char *NAME = "lnav_view_filter_stats";
    static constexpr const char *CREATE_STMT = R"(
-- Access statistics for filters through this table.
CREATE TABLE lnav_view_filter_stats (
    view_name TEXT,     -- The name of the view.
    filter_id INTEGER,  -- The filter identifier.
    hits      INTEGER   -- The number of lines that matched this filter.
);
)";

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        textview_curses &tc = lnav_data.ld_views[vc.iter.i_view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto tf = *(fs.begin() + vc.iter.i_filter_index);

        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[vc.iter.i_view_index], -1,
                                    SQLITE_STATIC);
                break;
            case 1:
                to_sqlite(ctx, tf->get_index());
                break;
            case 2:
                to_sqlite(ctx, tss->get_filtered_count_for(tf->get_index()));
                break;
        }

        return SQLITE_OK;
    }
};

struct lnav_view_files : public tvt_iterator_cursor<lnav_view_files> {
    static constexpr const char *NAME = "lnav_view_files";
    static constexpr const char *CREATE_STMT = R"(
--
CREATE TABLE lnav_view_files (
    view_name TEXT,     -- The name of the view.
    filepath  TEXT,     -- The path to the file.
    visible   INTEGER   -- Indicates whether or not the file is shown.
);
)";

    using iterator = logfile_sub_source::iterator;

    iterator begin() {
        return lnav_data.ld_log_source.begin();
    }

    iterator end() {
        return lnav_data.ld_log_source.end();
    }

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        auto& ld = *vc.iter;

        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[LNV_LOG], -1,
                                    SQLITE_STATIC);
                break;
            case 1:
                to_sqlite(ctx, ld->ld_filter_state.lfo_filter_state
                    .tfs_logfile->get_filename());
                break;
            case 2:
                to_sqlite(ctx, ld->ld_visible);
                break;
        }

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be deleted from the lnav_view_files table");
        return SQLITE_ERROR;
    }

    int insert_row(sqlite3_vtab *tab, sqlite3_int64 &rowid_out) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be inserted into the lnav_view_files table");
        return SQLITE_ERROR;
    };

    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid,
                   const char *view_name,
                   const char *file_path,
                   bool visible) {
        auto &lss = lnav_data.ld_log_source;
        auto iter = this->begin();

        std::advance(iter, rowid);

        auto& ld = *iter;
        if (ld->ld_visible != visible) {
            ld->set_visibility(visible);
            lss.text_filters_changed();
        }

        return SQLITE_OK;
    }
};

static const char *CREATE_FILTER_VIEW = R"(
CREATE VIEW lnav_view_filters_and_stats AS
  SELECT * FROM lnav_view_filters LEFT NATURAL JOIN lnav_view_filter_stats
)";

static auto a = injector::bind_multiple<vtab_module_base>()
    .add<vtab_module<lnav_views>>()
    .add<vtab_module<lnav_view_stack>>()
    .add<vtab_module<lnav_view_filters>>()
    .add<vtab_module<tvt_no_update<lnav_view_filter_stats>>>()
    .add<vtab_module<lnav_view_files>>();

int register_views_vtab(sqlite3 *db)
{
    char *errmsg;
    if (sqlite3_exec(db, CREATE_FILTER_VIEW, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        log_error("Unable to create filter view: %s", errmsg);
    }

    return 0;
}
