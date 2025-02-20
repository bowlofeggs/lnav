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
 * @file view_curses.hh
 */

#ifndef view_curses_hh
#define view_curses_hh

#include "config.h"

#include <zlib.h>
#include <stdint.h>
#include <limits.h>
#include <signal.h>
#include <sys/time.h>

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#else
#  error "SysV or X/Open-compatible Curses header file required"
#endif

#include <map>
#include <string>
#include <vector>
#include <functional>

#include "base/lnav_log.hh"
#include "base/lrucache.hpp"
#include "attr_line.hh"
#include "optional.hpp"
#include "styling.hh"
#include "log_level.hh"
#include "lnav_config_fwd.hh"

#define KEY_CTRL_G    7
#define KEY_CTRL_L    12
#define KEY_CTRL_P    16
#define KEY_CTRL_R    18
#define KEY_CTRL_W    23

class view_curses;

/**
 * An RAII class that initializes and deinitializes curses.
 */
class screen_curses : public log_crash_recoverer {
public:
    void log_crash_recover() override {
        endwin();
    };

    screen_curses()
        : sc_main_window(initscr()) {
    };

    virtual ~screen_curses()
    {
        endwin();
    };

    WINDOW *get_window() { return this->sc_main_window; };

private:
    WINDOW *sc_main_window;
};

template<typename T>
class action_broadcaster : public std::vector<std::function<void(T *)>> {
public:
    void operator()(T *t) {
        for (auto& func : *this) {
            func(t);
        }
    }
};

class ui_periodic_timer {
public:
    static const struct itimerval INTERVAL;

    static ui_periodic_timer &singleton();

    bool time_to_update(sig_atomic_t &counter) const {
        if (this->upt_counter != counter) {
            counter = this->upt_counter;
            return true;
        }
        return false;
    };

    void start_fade(sig_atomic_t &counter, size_t decay) const {
        counter = this->upt_counter + decay;
    };

    int fade_diff(sig_atomic_t &counter) const {
        if (this->upt_counter >= counter) {
            return 0;
        }
        return counter - this->upt_counter;
    };

private:
    ui_periodic_timer();

    static void sigalrm(int sig);

    volatile sig_atomic_t upt_counter;
};

class alerter {

public:
    static alerter &singleton();

    void enabled(bool enable) { this->a_enabled = enable; };

    bool chime() {
        if (!this->a_enabled) {
            return true;
        }

        bool retval = this->a_do_flash;
        if (this->a_do_flash) {
            ::flash();
        }
        this->a_do_flash = false;
        return retval;
    };

    void new_input(int ch) {
        if (this->a_last_input != ch) {
            this->a_do_flash = true;
        }
        this->a_last_input = ch;
    };

private:
    bool a_enabled{true};
    bool a_do_flash{true};
    int a_last_input{-1};
};

/**
 * Singleton used to manage the colorspace.
 */
class view_colors {
public:
    static constexpr unsigned long HI_COLOR_COUNT = 6 * 3 * 3;

    /** Roles that can be mapped to curses attributes using attrs_for_role() */
    typedef enum {
        VCR_NONE = -1,

        VCR_TEXT,               /*< Raw text. */
        VCR_IDENTIFIER,
        VCR_SEARCH,             /*< A search hit. */
        VCR_OK,
        VCR_ERROR,              /*< An error message. */
        VCR_WARNING,            /*< A warning message. */
        VCR_ALT_ROW,            /*< Highlight for alternating rows in a list */
        VCR_HIDDEN,
        VCR_ADJUSTED_TIME,
        VCR_SKEWED_TIME,
        VCR_OFFSET_TIME,
        VCR_INVALID_MSG,
        VCR_STATUS,             /*< Normal status line text. */
        VCR_WARN_STATUS,
        VCR_ALERT_STATUS,       /*< Alert status line text. */
        VCR_ACTIVE_STATUS,      /*< */
        VCR_ACTIVE_STATUS2,     /*< */
        VCR_STATUS_TITLE,
        VCR_STATUS_SUBTITLE,
        VCR_STATUS_STITCH_TITLE_TO_SUB,
        VCR_STATUS_STITCH_SUB_TO_TITLE,
        VCR_STATUS_STITCH_SUB_TO_NORMAL,
        VCR_STATUS_STITCH_NORMAL_TO_SUB,
        VCR_STATUS_STITCH_TITLE_TO_NORMAL,
        VCR_STATUS_STITCH_NORMAL_TO_TITLE,
        VCR_STATUS_TITLE_HOTKEY,
        VCR_STATUS_DISABLED_TITLE,
        VCR_STATUS_HOTKEY,
        VCR_INACTIVE_STATUS,
        VCR_INACTIVE_ALERT_STATUS,
        VCR_SCROLLBAR,
        VCR_SCROLLBAR_ERROR,
        VCR_SCROLLBAR_WARNING,
        VCR_FOCUSED,
        VCR_DISABLED_FOCUSED,
        VCR_POPUP,
        VCR_COLOR_HINT,

        VCR_KEYWORD,
        VCR_STRING,
        VCR_COMMENT,
        VCR_DOC_DIRECTIVE,
        VCR_VARIABLE,
        VCR_SYMBOL,
        VCR_NUMBER,
        VCR_RE_SPECIAL,
        VCR_RE_REPEAT,
        VCR_FILE,

        VCR_DIFF_DELETE,        /*< Deleted line in a diff. */
        VCR_DIFF_ADD,           /*< Added line in a diff. */
        VCR_DIFF_SECTION,       /*< Section marker in a diff. */

        VCR_LOW_THRESHOLD,
        VCR_MED_THRESHOLD,
        VCR_HIGH_THRESHOLD,

        VCR__MAX
    } role_t;

    /** @return A reference to the singleton. */
    static view_colors &singleton();

    /**
     * Performs curses-specific initialization.  The other methods can be
     * called before this method, but the returned attributes cannot be used
     * with curses code until this method is called.
     */
    static void init();

    void init_roles(const lnav_theme &lt, lnav_config_listener::error_reporter &reporter);

    /**
     * @param role The role to retrieve character attributes for.
     * @return The attributes to use for the given role.
     */
    attr_t attrs_for_role(role_t role, bool selected = false) const
    {
        if (role == VCR_NONE) {
            return 0;
        }

        require(role >= 0);
        require(role < VCR__MAX);

        return selected ? this->vc_role_colors[role].second :
               this->vc_role_colors[role].first;
    };

    attr_t reverse_attrs_for_role(role_t role) const
    {
        require(role >= 0);
        require(role < VCR__MAX);

        return this->vc_role_reverse_colors[role];
    };

    int color_for_ident(const char *str, size_t len) const;

    attr_t attrs_for_ident(const char *str, size_t len);

    attr_t attrs_for_ident(intern_string_t str) {
        return this->attrs_for_ident(str.get(), str.size());
    }

    attr_t attrs_for_ident(const std::string &str) {
        return this->attrs_for_ident(str.c_str(), str.length());
    };

    int ensure_color_pair(short fg, short bg);

    int ensure_color_pair(const styling::color_unit &fg,
                          const styling::color_unit &bg);

    static constexpr short MATCH_COLOR_DEFAULT = -1;
    static constexpr short MATCH_COLOR_SEMANTIC = -10;

    short match_color(const styling::color_unit &color) const;

    static inline int ansi_color_pair_index(int fg, int bg)
    {
        return VC_ANSI_START + ((fg * 8) + bg);
    };

    static inline attr_t ansi_color_pair(int fg, int bg)
    {
        return COLOR_PAIR(ansi_color_pair_index(fg, bg));
    };

    static const int VC_ANSI_START = 0;
    static const int VC_ANSI_END = VC_ANSI_START + (8 * 8);

    std::pair<attr_t, attr_t> to_attrs(
        int &pair_base,
        const lnav_theme &lt, const style_config &sc, const style_config &fallback_sc,
        lnav_config_listener::error_reporter &reporter);

    std::pair<attr_t, attr_t> vc_level_attrs[LEVEL__MAX];

    short ansi_to_theme_color(short ansi_fg) const {
        return this->vc_ansi_to_theme[ansi_fg];
    }

    static bool initialized;

private:
    static term_color_palette *vc_active_palette;

    /** Private constructor that initializes the member fields. */
    view_colors();

    struct dyn_pair {
        int dp_color_pair;
    };

    /** Map of role IDs to attribute values. */
    std::pair<attr_t, attr_t> vc_role_colors[VCR__MAX];
    /** Map of role IDs to reverse-video attribute values. */
    attr_t vc_role_reverse_colors[VCR__MAX];
    short vc_ansi_to_theme[8];
    short vc_highlight_colors[HI_COLOR_COUNT];
    int vc_color_pair_end{0};
    cache::lru_cache<std::pair<short, short>, dyn_pair> vc_dyn_pairs;
};

enum class mouse_button_t {
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT,

    BUTTON_SCROLL_UP,
    BUTTON_SCROLL_DOWN,
};

enum class mouse_button_state_t {
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_DRAGGED,
    BUTTON_STATE_RELEASED,
};

struct mouse_event {
    mouse_event(mouse_button_t button = mouse_button_t::BUTTON_LEFT,
                mouse_button_state_t state = mouse_button_state_t::BUTTON_STATE_PRESSED,
                int x = -1,
                int y = -1)
            : me_button(button),
              me_state(state),
              me_x(x),
              me_y(y) {
        memset(&this->me_time, 0, sizeof(this->me_time));
    };

    mouse_button_t me_button;
    mouse_button_state_t me_state;
    struct timeval me_time;
    int me_x;
    int me_y;
};

/**
 * Interface for "view" classes that will update a curses(3) display.
 */
class view_curses {
public:
    virtual ~view_curses() = default;

    /**
     * Update the curses display.
     */
    virtual void do_update() {
        if (!this->vc_visible) {
            return;
        }

        for (auto child : this->vc_children) {
            child->do_update();
        }
    };

    virtual bool handle_mouse(mouse_event &me) { return false; };

    void set_needs_update() {
        this->vc_needs_update = true;
        for (auto child : this->vc_children) {
            child->set_needs_update();
        }
    };

    view_curses &add_child_view(view_curses *child) {
        this->vc_children.push_back(child);

        return *this;
    }

    void set_default_role(view_colors::role_t role) {
        this->vc_default_role = role;
    }

    void set_visible(bool value) {
        this->vc_visible = value;
    }

    bool is_visible() const {
        return this->vc_visible;
    }

    void set_width(long width) {
        this->vc_width = width;
    }

    long get_width() const {
        return this->vc_width;
    }

    static string_attr_type VC_ROLE;
    static string_attr_type VC_ROLE_FG;
    static string_attr_type VC_STYLE;
    static string_attr_type VC_GRAPHIC;
    static string_attr_type VC_SELECTED;
    static string_attr_type VC_FOREGROUND;
    static string_attr_type VC_BACKGROUND;

    static void awaiting_user_input();

    static void mvwattrline(WINDOW *window,
                            int y,
                            int x,
                            attr_line_t &al,
                            const struct line_range &lr,
                            view_colors::role_t base_role =
                                view_colors::VCR_TEXT);

protected:
    bool vc_visible{true};
    /** Flag to indicate if a display update is needed. */
    bool vc_needs_update{true};
    long vc_width;
    std::vector<view_curses *> vc_children;
    view_colors::role_t vc_default_role{view_colors::VCR_TEXT};
};

template<class T>
class view_stack : public view_curses {
public:
    using iterator = typename std::vector<T *>::iterator;

    nonstd::optional<T *> top() {
        if (this->vs_views.empty()) {
            return nonstd::nullopt;
        } else {
            return this->vs_views.back();
        }
    }

    void do_update() override
    {
        if (!this->vc_visible) {
            return;
        }

        this->top() | [this] (T *vc) {
            if (this->vc_needs_update) {
                vc->set_needs_update();
            }
            vc->do_update();
        };

        view_curses::do_update();

        this->vc_needs_update = false;
    }

    void push_back(T *view) {
        this->vs_views.push_back(view);
        if (this->vs_change_handler) {
            this->vs_change_handler(view);
        }
        this->set_needs_update();
    }

    void pop_back() {
        this->vs_views.pop_back();
        if (!this->vs_views.empty() && this->vs_change_handler) {
            this->vs_change_handler(this->vs_views.back());
        }
        this->set_needs_update();
    }

    iterator begin() {
        return this->vs_views.begin();
    }

    iterator end() {
        return this->vs_views.end();
    }

    size_t size() {
        return this->vs_views.size();
    }

    bool empty() {
        return this->vs_views.empty();
    }

    std::function<void(T *)> vs_change_handler;

private:
    std::vector<T *> vs_views;
};

#endif
