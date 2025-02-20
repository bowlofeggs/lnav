/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file state-extension-functions.cc
 */

#include "config.h"

#include <stdint.h>

#include <string>

#include "sqlite3.h"

#include "lnav.hh"
#include "sql_util.hh"
#include "vtab_module.hh"

static nonstd::optional<int64_t> sql_log_top_line()
{
    const auto& tc = lnav_data.ld_views[LNV_LOG];

    if (tc.get_inner_height() == 0_vl) {
        return nonstd::nullopt;
    }
    return (int64_t) tc.get_top();
}

static nonstd::optional<std::string> sql_log_top_datetime()
{
    const auto& tc = lnav_data.ld_views[LNV_LOG];

    if (tc.get_inner_height() == 0_vl) {
        return nonstd::nullopt;
    }

    auto top_time = lnav_data.ld_log_source.time_for_row(lnav_data.ld_views[LNV_LOG].get_top());
    if (!top_time) {
        return nonstd::nullopt;
    }

    char buffer[64];

    sql_strftime(buffer, sizeof(buffer), top_time.value());
    return buffer;
}

static nonstd::optional<std::string> sql_lnav_top_file()
{
    auto top_view_opt = lnav_data.ld_view_stack.top();

    if (!top_view_opt) {
        return nonstd::nullopt;
    }

    auto top_view = top_view_opt.value();
    return top_view->map_top_row([](const auto& al) {
        return get_string_attr(al.get_attrs(), &logline::L_FILE) | [](const auto* sa) {
            auto lf = (logfile *) sa->sa_value.sav_ptr;

            return nonstd::make_optional(lf->get_filename());
        };
    });
}

static int64_t sql_error(const char *str)
{
    throw sqlite_func_error("{}", str);
}

int state_extension_functions(struct FuncDef **basic_funcs,
                              struct FuncDefAgg **agg_funcs)
{
    static struct FuncDef state_funcs[] = {
        sqlite_func_adapter<decltype(&sql_log_top_line), sql_log_top_line>::builder(
            help_text("log_top_line",
                      "Return the line number at the top of the log view.")
                .sql_function()
        ),

        sqlite_func_adapter<decltype(&sql_log_top_datetime), sql_log_top_datetime>::builder(
            help_text("log_top_datetime",
                      "Return the timestamp of the line at the top of the log view.")
                .sql_function()
        ),

        sqlite_func_adapter<decltype(&sql_lnav_top_file), sql_lnav_top_file>::builder(
            help_text("lnav_top_file",
                      "Return the name of the file that the top line in the current view came from.")
                .sql_function()
        ),

        sqlite_func_adapter<decltype(&sql_error), sql_error>::builder(
            help_text("raise_error",
                      "Raises an error with the given message when executed")
                .sql_function()
                .with_parameter({"msg", "The error message"})
        ).with_flags(SQLITE_UTF8),

        { nullptr }
    };

    *basic_funcs = state_funcs;

    return SQLITE_OK;
}
