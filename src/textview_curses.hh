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
 *
 * @file textview_curses.hh
 */

#ifndef textview_curses_hh
#define textview_curses_hh

#include <utility>
#include <vector>

#include "base/func_util.hh"
#include "ring_span.hh"
#include "grep_proc.hh"
#include "bookmarks.hh"
#include "listview_curses.hh"
#include "base/lnav_log.hh"
#include "text_format.hh"
#include "logfile.hh"
#include "highlighter.hh"
#include "lnav_config_fwd.hh"
#include "textview_curses_fwd.hh"

class logline;
class textview_curses;

using vis_bookmarks = bookmarks<vis_line_t>::type;

class logfile_filter_state {
public:
    logfile_filter_state(std::shared_ptr<logfile> lf = nullptr) : tfs_logfile(
        std::move(lf)) {
        memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
        memset(this->tfs_filter_hits, 0, sizeof(this->tfs_filter_hits));
        memset(this->tfs_message_matched, 0, sizeof(this->tfs_message_matched));
        memset(this->tfs_lines_for_message, 0, sizeof(this->tfs_lines_for_message));
        memset(this->tfs_last_message_matched, 0, sizeof(this->tfs_last_message_matched));
        memset(this->tfs_last_lines_for_message, 0, sizeof(this->tfs_last_lines_for_message));
        this->tfs_mask.reserve(64 * 1024);
    };

    void clear() {
        this->tfs_logfile = nullptr;
        memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
        memset(this->tfs_filter_hits, 0, sizeof(this->tfs_filter_hits));
        memset(this->tfs_message_matched, 0, sizeof(this->tfs_message_matched));
        memset(this->tfs_lines_for_message, 0, sizeof(this->tfs_lines_for_message));
        memset(this->tfs_last_message_matched, 0, sizeof(this->tfs_last_message_matched));
        memset(this->tfs_last_lines_for_message, 0, sizeof(this->tfs_last_lines_for_message));
        this->tfs_mask.clear();
        this->tfs_index.clear();
    };

    void clear_filter_state(size_t index) {
        this->tfs_filter_count[index] = 0;
        this->tfs_filter_hits[index] = 0;
        this->tfs_message_matched[index] = false;
        this->tfs_lines_for_message[index] = 0;
        this->tfs_last_message_matched[index] = false;
        this->tfs_last_lines_for_message[index] = 0;
    };

    void clear_deleted_filter_state(uint32_t used_mask) {
        for (int lpc = 0; lpc < MAX_FILTERS; lpc++) {
            if (!(used_mask & (1L << lpc))) {
                this->clear_filter_state(lpc);
            }
        }
        for (size_t lpc = 0; lpc < this->tfs_mask.size(); lpc++) {
            this->tfs_mask[lpc] &= used_mask;
        }
    }

    void resize(size_t newsize) {
        size_t old_mask_size = this->tfs_mask.size();

        this->tfs_mask.resize(newsize);
        if (newsize > old_mask_size) {
            memset(&this->tfs_mask[old_mask_size],
                    0,
                    sizeof(uint32_t) * (newsize - old_mask_size));
        }
    };

    const static int MAX_FILTERS = 32;

    std::shared_ptr<logfile> tfs_logfile;
    size_t tfs_filter_count[MAX_FILTERS];
    int tfs_filter_hits[MAX_FILTERS];
    bool tfs_message_matched[MAX_FILTERS];
    size_t tfs_lines_for_message[MAX_FILTERS];
    bool tfs_last_message_matched[MAX_FILTERS];
    size_t tfs_last_lines_for_message[MAX_FILTERS];
    std::vector<uint32_t> tfs_mask;
    std::vector<uint32_t> tfs_index;
};

enum class filter_lang_t : int {
    NONE,
    REGEX,
    SQL,
};

class text_filter {
public:
    typedef enum {
        MAYBE,
        INCLUDE,
        EXCLUDE,

        LFT__MAX,

        LFT__MASK = (MAYBE|INCLUDE|EXCLUDE)
    } type_t;

    text_filter(type_t type, filter_lang_t lang, std::string id, size_t index)
            : lf_type(type),
              lf_lang(lang),
              lf_id(std::move(id)),
              lf_index(index) { };
    virtual ~text_filter() = default;

    type_t get_type() const { return this->lf_type; }
    filter_lang_t get_lang() const { return this->lf_lang; }
    void set_type(type_t t) { this->lf_type = t; };
    std::string get_id() const { return this->lf_id; };
    void set_id(std::string id) {
        this->lf_id = std::move(id);
    }
    size_t get_index() const { return this->lf_index; };

    bool is_enabled() const { return this->lf_enabled; };
    void enable() { this->lf_enabled = true; };
    void disable() { this->lf_enabled = false; };
    void set_enabled(bool value) {
        this->lf_enabled = value;
    }

    void revert_to_last(logfile_filter_state &lfs, size_t rollback_size);

    void add_line(logfile_filter_state &lfs, logfile::const_iterator ll, shared_buffer_ref &line);

    void end_of_message(logfile_filter_state &lfs);

    virtual bool matches(const logfile &lf, logfile::const_iterator ll, shared_buffer_ref &line) = 0;

    virtual std::string to_command() = 0;

    bool operator==(const std::string &rhs) {
        return this->lf_id == rhs;
    };

    bool lf_deleted{false};

protected:
    bool        lf_enabled{true};
    type_t      lf_type;
    filter_lang_t lf_lang;
    std::string lf_id;
    size_t lf_index;
};

class empty_filter : public text_filter {
public:
    empty_filter(type_t type, size_t index)
        : text_filter(type, filter_lang_t::REGEX, "", index) {
    }

    bool matches(const logfile &lf, logfile::const_iterator ll,
                 shared_buffer_ref &line) override;

    std::string to_command() override;
};

class filter_stack {
public:
    typedef std::vector<std::shared_ptr<text_filter>>::iterator iterator;

    explicit filter_stack(size_t reserved = 0) : fs_reserved(reserved) {
    }

    iterator begin() {
        return this->fs_filters.begin();
    }

    iterator end() {
        return this->fs_filters.end();
    }

    size_t size() const {
        return this->fs_filters.size();
    }

    bool empty() const {
        return this->fs_filters.empty();
    };

    bool full() const {
        return (this->fs_reserved + this->fs_filters.size()) ==
               logfile_filter_state::MAX_FILTERS;
    }

    nonstd::optional<size_t> next_index() {
        bool used[32];

        memset(used, 0, sizeof(used));
        for (auto &iter : *this) {
            if (iter->lf_deleted) {
                continue;
            }

            size_t index = iter->get_index();

            require(used[index] == false);

            used[index] = true;
        }
        for (size_t lpc = this->fs_reserved;
             lpc < logfile_filter_state::MAX_FILTERS;
             lpc++) {
            if (!used[lpc]) {
                return lpc;
            }
        }
        return nonstd::nullopt;
    };

    void add_filter(const std::shared_ptr<text_filter> &filter) {
        this->fs_filters.push_back(filter);
    };

    void clear_filters() {
        while (!this->fs_filters.empty()) {
            this->fs_filters.pop_back();
        }
    };

    void set_filter_enabled(const std::shared_ptr<text_filter> &filter, bool enabled) {
        if (enabled) {
            filter->enable();
        }
        else {
            filter->disable();
        }
    }

    std::shared_ptr<text_filter> get_filter(const std::string &id)
    {
        auto iter = this->fs_filters.begin();
        std::shared_ptr<text_filter> retval;

        for (;
             iter != this->fs_filters.end() && (*iter)->get_id() != id;
             iter++) { }
        if (iter != this->fs_filters.end()) {
            retval = *iter;
        }

        return retval;
    };

    bool delete_filter(const std::string &id) {
        auto iter = this->fs_filters.begin();

        for (;
             iter != this->fs_filters.end() && (*iter)->get_id() != id;
             iter++) {

        }
        if (iter != this->fs_filters.end()) {
            this->fs_filters.erase(iter);
            return true;
        }

        return false;
    };

    void get_mask(uint32_t &filter_mask) {
        filter_mask = 0;
        for (auto &iter : *this) {
            std::shared_ptr<text_filter> tf = iter;

            if (tf->lf_deleted) {
                continue;
            }
            if (tf->is_enabled()) {
                uint32_t bit = (1UL << tf->get_index());

                switch (tf->get_type()) {
                    case text_filter::EXCLUDE:
                    case text_filter::INCLUDE:
                        filter_mask |= bit;
                        break;
                    default:
                        ensure(0);
                        break;
                }
            }
        }
    }

    void get_enabled_mask(uint32_t &filter_in_mask, uint32_t &filter_out_mask) {
        filter_in_mask = filter_out_mask = 0;
        for (auto &iter : *this) {
            std::shared_ptr<text_filter> tf = iter;

            if (tf->lf_deleted) {
                continue;
            }
            if (tf->is_enabled()) {
                uint32_t bit = (1UL << tf->get_index());

                switch (tf->get_type()) {
                    case text_filter::EXCLUDE:
                        filter_out_mask |= bit;
                        break;
                    case text_filter::INCLUDE:
                        filter_in_mask |= bit;
                        break;
                    default:
                        ensure(0);
                        break;
                }
            }
        }
    };

private:
    const size_t fs_reserved;
    std::vector<std::shared_ptr<text_filter>> fs_filters;
};

class text_time_translator {
public:
    virtual ~text_time_translator() = default;

    virtual nonstd::optional<vis_line_t> row_for_time(struct timeval time_bucket) = 0;

    virtual nonstd::optional<struct timeval> time_for_row(vis_line_t row) = 0;

    void scroll_invoked(textview_curses *tc);

    void data_reloaded(textview_curses *tc);
protected:
    struct timeval ttt_top_time{0, 0};
};

class location_history {
public:
    virtual ~location_history() = default;

    virtual void loc_history_append(vis_line_t top) = 0;

    virtual nonstd::optional<vis_line_t>
    loc_history_back(vis_line_t current_top) = 0;

    virtual nonstd::optional<vis_line_t>
    loc_history_forward(vis_line_t current_top) = 0;

    const static int MAX_SIZE = 100;
protected:
    size_t lh_history_position{0};
};

/**
 * Source for the text to be shown in a textview_curses view.
 */
class text_sub_source {
public:
    virtual ~text_sub_source() = default;

    enum {
        RB_RAW,
        RB_FULL,
        RB_REWRITE,
    };

    enum {
        RF_RAW = (1UL << RB_RAW),
        RF_FULL = (1UL << RB_FULL),
        RF_REWRITE = (1UL << RB_REWRITE),
    };

    typedef long line_flags_t;

    text_sub_source(size_t reserved_filters = 0)
        : tss_filters(reserved_filters) {
    }

    void register_view(textview_curses *tc) {
        this->tss_view = tc;
    };

    /**
     * @return The total number of lines available from the source.
     */
    virtual size_t text_line_count() = 0;

    virtual size_t text_line_width(textview_curses &curses) {
        return INT_MAX;
    };

    /**
     * Get the value for a line.
     *
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out The string object that should be set to the line
     *   contents.
     * @param raw Indicates that the raw contents of the line should be returned
     *   without any post processing.
     */
    virtual void text_value_for_line(textview_curses &tc,
                                     int line,
                                     std::string &value_out,
                                     line_flags_t flags = 0) = 0;

    virtual size_t text_size_for_line(textview_curses &tc, int line, line_flags_t raw = 0) = 0;

    /**
     * Inform the source that the given line has been marked/unmarked.  This
     * callback function can be used to translate between between visible line
     * numbers and content line numbers.  For example, when viewing a log file
     * with filters being applied, we want the bookmarked lines to be stable
     * across changes in the filters.
     *
     * @param bm    The type of bookmark.
     * @param line  The line that has been marked/unmarked.
     * @param added True if the line was bookmarked and false if it was
     *   unmarked.
     */
    virtual void text_mark(bookmark_type_t *bm, vis_line_t line, bool added) {};

    /**
     * Clear the bookmarks for a particular type in the text source.
     *
     * @param bm The type of bookmarks to clear.
     */
    virtual void text_clear_marks(bookmark_type_t *bm) {};

    /**
     * Get the attributes for a line of text.
     *
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out A string_attrs_t object that should be updated with the
     *   attributes for the line.
     */
    virtual void text_attrs_for_line(textview_curses &tc,
                                     int line,
                                     string_attrs_t &value_out) {};

    /**
     * Update the bookmarks used by the text view based on the bookmarks
     * maintained by the text source.
     *
     * @param bm The bookmarks data structure used by the text view.
     */
    virtual void text_update_marks(vis_bookmarks &bm) { };

    virtual std::string text_source_name(const textview_curses &tv) {
        return "";
    };

    filter_stack &get_filters() {
        return this->tss_filters;
    };

    virtual void text_filters_changed() {

    };

    virtual int get_filtered_count() const {
        return 0;
    };

    virtual int get_filtered_count_for(size_t filter_index) const {
        return 0;
    }

    virtual text_format_t get_text_format() const {
        return text_format_t::TF_UNKNOWN;
    };

    virtual nonstd::optional<std::pair<grep_proc_source<vis_line_t> *, grep_proc_sink<vis_line_t> *>> get_grepper() {
        return nonstd::nullopt;
    }

    virtual nonstd::optional<location_history *> get_location_history() {
        return nonstd::nullopt;
    }

    void toggle_apply_filters() {
        this->tss_apply_filters = !this->tss_apply_filters;
        this->text_filters_changed();
    }

    bool tss_supports_filtering{false};
    bool tss_apply_filters{true};
protected:
    textview_curses *tss_view{nullptr};
    filter_stack tss_filters;
};

class vis_location_history : public location_history {
public:
    vis_location_history()
        : vlh_history(std::begin(this->vlh_backing), std::end(this->vlh_backing))
    {
    }

    void loc_history_append(vis_line_t top) override {
        auto iter = this->vlh_history.begin();
        iter += this->vlh_history.size() - this->lh_history_position;
        this->vlh_history.erase_from(iter);
        this->lh_history_position = 0;
        this->vlh_history.push_back(top);
    }

    nonstd::optional<vis_line_t>
    loc_history_back(vis_line_t current_top) override {
        if (this->lh_history_position == 0) {
            vis_line_t history_top = this->current_position();
            if (history_top != current_top) {
                return history_top;
            }
        }

        if (this->lh_history_position + 1 >= this->vlh_history.size()) {
            return nonstd::nullopt;
        }

        this->lh_history_position += 1;

        return this->current_position();
    }

    nonstd::optional<vis_line_t>
    loc_history_forward(vis_line_t current_top) override {
        if (this->lh_history_position == 0) {
            return nonstd::nullopt;
        }

        this->lh_history_position -= 1;

        return this->current_position();
    }

    nonstd::ring_span<vis_line_t> vlh_history;
private:
    vis_line_t current_position() {
        auto iter = this->vlh_history.rbegin();

        iter += this->lh_history_position;

        return *iter;
    }

    vis_line_t vlh_backing[MAX_SIZE];
};

class text_delegate {
public:
    virtual ~text_delegate() = default;
    
    virtual void text_overlay(textview_curses &tc) { };

    virtual bool text_handle_mouse(textview_curses &tc, mouse_event &me) {
        return false;
    };
};

/**
 * The textview_curses class adds user bookmarks and searching to the standard
 * list view interface.
 */
class textview_curses
    : public listview_curses,
      public list_data_source,
      public grep_proc_source<vis_line_t>,
      public grep_proc_sink<vis_line_t>,
      public lnav_config_listener {
public:

    using action = std::function<void(textview_curses*)>;

    static bookmark_type_t BM_USER;
    static bookmark_type_t BM_USER_EXPR;
    static bookmark_type_t BM_SEARCH;
    static bookmark_type_t BM_META;

    textview_curses();

    void reload_config(error_reporter &reporter);

    void set_paused(bool paused) {
        this->tc_paused = paused;
        if (this->tc_state_event_handler) {
            this->tc_state_event_handler(*this);
        }
    }

    bool is_paused() const {
        return this->tc_paused;
    }

    vis_bookmarks &get_bookmarks() { return this->tc_bookmarks; };

    const vis_bookmarks &get_bookmarks() const { return this->tc_bookmarks; };

    void toggle_user_mark(bookmark_type_t *bm,
                          vis_line_t start_line,
                          vis_line_t end_line = vis_line_t(-1));

    void set_user_mark(bookmark_type_t *bm, vis_line_t vl, bool marked);

    textview_curses &set_sub_source(text_sub_source *src) {
        this->tc_sub_source = src;
        if (src) {
            src->register_view(this);
        }
        this->reload_data();
        return *this;
    };

    text_sub_source *get_sub_source() const { return this->tc_sub_source; };

    textview_curses &set_delegate(text_delegate *del) {
        this->tc_delegate = del;

        return *this;
    };

    text_delegate *get_delegate() const { return this->tc_delegate; };

    void horiz_shift(vis_line_t start, vis_line_t end,
                     int off_start,
                     std::pair<int, int> &range_out);

    void set_search_action(action sa) { this->tc_search_action = std::move(sa); };

    void grep_end_batch(grep_proc<vis_line_t> &gp);
    void grep_end(grep_proc<vis_line_t> &gp);

    size_t listview_rows(const listview_curses &lv)
    {
        return this->tc_sub_source == nullptr ? 0 :
               this->tc_sub_source->text_line_count();
    };

    size_t listview_width(const listview_curses &lv) {
        return this->tc_sub_source == nullptr ? 0 :
               this->tc_sub_source->text_line_width(*this);
    };

    void listview_value_for_rows(const listview_curses &lv,
                                 vis_line_t line,
                                 std::vector<attr_line_t> &rows_out);

    void textview_value_for_row(vis_line_t line, attr_line_t &value_out);

    size_t listview_size_for_row(const listview_curses &lv, vis_line_t row) {
        return this->tc_sub_source->text_size_for_line(*this, row);
    };

    std::string listview_source_name(const listview_curses &lv) {
        return this->tc_sub_source == nullptr ? "" :
               this->tc_sub_source->text_source_name(*this);
    };

    bool grep_value_for_line(vis_line_t line, std::string &value_out)
    {
        bool retval = false;

        if (this->tc_sub_source &&
            line < (int)this->tc_sub_source->text_line_count()) {
            this->tc_sub_source->text_value_for_line(*this,
                                                     line,
                                                     value_out,
                                                     text_sub_source::RF_RAW);
            retval = true;
        }

        return retval;
    };

    void grep_begin(grep_proc<vis_line_t> &gp, vis_line_t start, vis_line_t stop);
    void grep_match(grep_proc<vis_line_t> &gp,
                    vis_line_t line,
                    int start,
                    int end);

    bool is_searching() const { return this->tc_searching > 0; };

    void set_follow_search_for(int64_t ms_to_deadline,
                               std::function<bool()> func) {
        struct timeval now, tv;

        tv.tv_sec = ms_to_deadline / 1000;
        tv.tv_usec = (ms_to_deadline % 1000) * 1000;
        gettimeofday(&now, nullptr);
        timeradd(&now, &tv, &this->tc_follow_deadline);
        this->tc_follow_top = this->get_top();
        this->tc_follow_func = func;
    };

    size_t get_match_count()
    {
        return this->tc_bookmarks[&BM_SEARCH].size();
    };

    void match_reset()
    {
        this->tc_bookmarks[&BM_SEARCH].clear();
        if (this->tc_sub_source != nullptr) {
            this->tc_sub_source->text_clear_marks(&BM_SEARCH);
        }
    };

    highlight_map_t &get_highlights() { return this->tc_highlights; };

    const highlight_map_t &get_highlights() const { return this->tc_highlights; };

    std::set<highlight_source_t> &get_disabled_highlights() {
        return this->tc_disabled_highlights;
    }

    bool handle_mouse(mouse_event &me);

    void reload_data();

    void do_update() {
        this->listview_curses::do_update();
        if (this->tc_delegate != nullptr) {
            this->tc_delegate->text_overlay(*this);
        }
    };

    bool toggle_hide_fields() {
        bool retval = this->tc_hide_fields;

        this->tc_hide_fields = !this->tc_hide_fields;

        return retval;
    };

    bool get_hide_fields() const {
        return this->tc_hide_fields;
    }

    void execute_search(const std::string &regex_orig);

    void redo_search() {
        if (this->tc_search_child) {
            grep_proc<vis_line_t> *gp = this->tc_search_child->get_grep_proc();

            gp->invalidate();
            this->match_reset();
            gp->queue_request(0_vl)
              .start();

            if (this->tc_source_search_child) {
                this->tc_source_search_child->invalidate()
                    .queue_request(0_vl)
                    .start();
            }
        }
    };

    void search_range(vis_line_t start, vis_line_t stop = -1_vl) {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->queue_request(start, stop);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->queue_request(start, stop);
        }
    }

    void search_new_data(vis_line_t start = -1_vl) {
        this->search_range(start);
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->start();
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->start();
        }
    }

    void update_poll_set(std::vector<struct pollfd> &pollfds) {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->update_poll_set(pollfds);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->update_poll_set(pollfds);
        }
    }

    void check_poll_set(const std::vector<struct pollfd> &pollfds) {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->check_poll_set(pollfds);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->check_poll_set(pollfds);
        }
    }

    std::string get_current_search() const {
        return this->tc_current_search;
    }

    void revert_search() {
        this->execute_search(this->tc_previous_search);
    }

    void invoke_scroll() {
        if (this->tc_sub_source != nullptr) {
            auto ttt = dynamic_cast<text_time_translator *>(this->tc_sub_source);

            if (ttt != nullptr) {
                ttt->scroll_invoked(this);
            }
        }

        listview_curses::invoke_scroll();
    }

    std::function<void(textview_curses &)> tc_state_event_handler;

protected:

    class grep_highlighter {
    public:
        grep_highlighter(std::unique_ptr<grep_proc<vis_line_t>> &gp,
                         highlight_source_t source,
                         std::string hl_name,
                         highlight_map_t &hl_map)
            : gh_grep_proc(std::move(gp)),
              gh_hl_source(source),
              gh_hl_name(std::move(hl_name)),
              gh_hl_map(hl_map) { };

        ~grep_highlighter()
        {
            this->gh_hl_map.erase(this->gh_hl_map.find(
                {this->gh_hl_source, this->gh_hl_name}));
        };

        grep_proc<vis_line_t> *get_grep_proc() { return this->gh_grep_proc.get(); };

    private:
        std::unique_ptr<grep_proc<vis_line_t>> gh_grep_proc;
        highlight_source_t gh_hl_source;
        std::string gh_hl_name;
        highlight_map_t &gh_hl_map;
    };

    text_sub_source *tc_sub_source{nullptr};
    text_delegate *tc_delegate{nullptr};

    vis_bookmarks tc_bookmarks;

    int tc_searching{0};
    struct timeval tc_follow_deadline{0, 0};
    vis_line_t tc_follow_top{-1_vl};
    std::function<bool()> tc_follow_func;
    action tc_search_action;

    highlight_map_t tc_highlights;
    std::set<highlight_source_t> tc_disabled_highlights;

    vis_line_t tc_selection_start{-1_vl};
    vis_line_t tc_selection_last{-1_vl};
    bool tc_selection_cleared{false};
    bool tc_hide_fields{true};
    bool tc_paused{false};

    std::string tc_current_search;
    std::string tc_previous_search;
    std::unique_ptr<grep_highlighter> tc_search_child;
    std::shared_ptr<grep_proc<vis_line_t>> tc_source_search_child;
};

#endif
