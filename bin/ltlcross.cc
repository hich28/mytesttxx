// -*- coding: utf-8 -*-
// Copyright (C) 2012, 2013, 2014, 2015, 2016, 2017 Laboratoire de Recherche et
// Développement de l'Epita (LRDE).
//
// This file is part of Spot, a model checking library.
//
// Spot is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// Spot is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


#include "common_sys.hh"

#include <string>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <argp.h>
#include <unistd.h>
#include <cmath>
#include <sys/wait.h>
#include "error.h"
#include "argmatch.h"

#include "common_setup.hh"
#include "common_cout.hh"
#include "common_conv.hh"
#include "common_trans.hh"
#include "common_file.hh"
#include "common_finput.hh"
#include "common_hoaread.hh"
#include "common_aoutput.hh"
#include <spot/parseaut/public.hh>
#include <spot/tl/print.hh>
#include <spot/tl/apcollect.hh>
#include <spot/tl/mutation.hh>
#include <spot/tl/relabel.hh>
#include <spot/twaalgos/lbtt.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/remfin.hh>
#include <spot/twaalgos/gtec/gtec.hh>
#include <spot/twaalgos/randomgraph.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <spot/twaalgos/isweakscc.hh>
#include <spot/twaalgos/word.hh>
#include <spot/twaalgos/dualize.hh>
#include <spot/twaalgos/cleanacc.hh>
#include <spot/twaalgos/alternation.hh>
#include <spot/misc/formater.hh>
#include <spot/twaalgos/stats.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/isunamb.hh>
#include <spot/twaalgos/postproc.hh>
#include <spot/misc/escape.hh>
#include <spot/misc/hash.hh>
#include <spot/misc/random.hh>
#include <spot/misc/tmpfile.hh>
#include <spot/misc/timer.hh>

const char argp_program_doc[] ="\
Call several LTL/PSL translators and cross-compare their output to detect \
bugs, or to gather statistics.  The list of formulas to use should be \
supplied on standard input, or using the -f or -F options.\v\
Exit status:\n\
  0  everything went fine (timeouts are OK too)\n\
  1  some translator failed to output something we understand, or failed\n\
     sanity checks (statistics were output nonetheless)\n\
  2  ltlcross aborted on error\n\
";

enum {
  OPT_AMBIGUOUS = 256,
  OPT_AUTOMATA,
  OPT_BOGUS,
  OPT_COLOR,
  OPT_CSV,
  OPT_DENSITY,
  OPT_DUPS,
  OPT_GRIND,
  OPT_IGNORE_EXEC_FAIL,
  OPT_JSON,
  OPT_NOCHECKS,
  OPT_NOCOMP,
  OPT_OMIT,
  OPT_PRODUCTS,
  OPT_SEED,
  OPT_STATES,
  OPT_STOP_ERR,
  OPT_STRENGTH,
  OPT_VERBOSE,
};

static const argp_option options[] =
  {
    /**************************************************/
    { nullptr, 0, nullptr, 0, "ltlcross behavior:", 5 },
    { "allow-dups", OPT_DUPS, nullptr, 0,
      "translate duplicate formulas in input", 0 },
    { "no-checks", OPT_NOCHECKS, nullptr, 0,
      "do not perform any sanity checks (negated formulas "
      "will not be translated)", 0 },
    { "no-complement", OPT_NOCOMP, nullptr, 0,
      "do not complement deterministic automata to perform extra checks", 0 },
    { "determinize", 'D', nullptr, 0,
      "determinize non-deterministic automata so that they"
      "can be complemented; also implicitly sets --products=0", 0 },
    { "stop-on-error", OPT_STOP_ERR, nullptr, 0,
      "stop on first execution error or failure to pass"
      " sanity checks (timeouts are OK)", 0 },
    { "ignore-execution-failures", OPT_IGNORE_EXEC_FAIL, nullptr, 0,
      "ignore automata from translators that return with a non-zero exit code,"
      " but do not flag this as an error", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "State-space generation:", 6 },
    { "states", OPT_STATES, "INT", 0,
      "number of the states in the state-spaces (200 by default)", 0 },
    { "density", OPT_DENSITY, "FLOAT", 0,
      "probability, between 0.0 and 1.0, to add a transition between "
      "two states (0.1 by default)", 0 },
    { "seed", OPT_SEED, "INT", 0,
      "seed for the random number generator (0 by default)", 0 },
    { "products", OPT_PRODUCTS, "[+]INT", 0,
      "number of products to perform (1 by default), statistics will be "
      "averaged unless the number is prefixed with '+'", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Statistics output:", 7 },
    { "json", OPT_JSON, "[>>]FILENAME", OPTION_ARG_OPTIONAL,
      "output statistics as JSON in FILENAME or on standard output", 0 },
    { "csv", OPT_CSV, "[>>]FILENAME", OPTION_ARG_OPTIONAL,
      "output statistics as CSV in FILENAME or on standard output "
      "(if '>>' is used to request append mode, the header line is "
      "not output)", 0 },
    { "omit-missing", OPT_OMIT, nullptr, 0,
      "do not output statistics for timeouts or failed translations", 0 },
    { "automata", OPT_AUTOMATA, nullptr, 0,
      "store automata (in the HOA format) into the CSV or JSON output", 0 },
    { "strength", OPT_STRENGTH, nullptr, 0,
      "output statistics about SCC strengths (non-accepting, terminal, weak, "
      "strong) [not supported for alternating automata]", 0 },
    { "ambiguous", OPT_AMBIGUOUS, nullptr, 0,
      "output statistics about ambiguous automata "
      "[not supported for alternating automata]", 0 },
    { "unambiguous", 0, nullptr, OPTION_ALIAS, nullptr, 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Miscellaneous options:", -2 },
    { "color", OPT_COLOR, "WHEN", OPTION_ARG_OPTIONAL,
      "colorize output; WHEN can be 'never', 'always' (the default if "
      "--color is used without argument), or "
      "'auto' (the default if --color is not used)", 0 },
    { "grind", OPT_GRIND, "[>>]FILENAME", 0,
      "for each formula for which a problem was detected, write a simpler " \
      "formula that fails on the same test in FILENAME", 0 },
    { "save-bogus", OPT_BOGUS, "[>>]FILENAME", 0,
      "save formulas for which problems were detected in FILENAME", 0 },
    { "verbose", OPT_VERBOSE, nullptr, 0,
      "print what is being done, for debugging", 0 },
    { nullptr, 0, nullptr, 0,
      "If an output FILENAME is prefixed with '>>', is it open "
      "in append mode instead of being truncated.", -1 },
    { nullptr, 0, nullptr, 0, nullptr, 0 }
  };

const struct argp_child children[] =
  {
    { &finput_argp, 0, nullptr, 1 },
    { &trans_argp, 0, nullptr, 0 },
    { &hoaread_argp, 0, "Parsing of automata:", 4 },
    { &misc_argp, 0, nullptr, -2 },
    { nullptr, 0, nullptr, 0 }
  };

enum color_type { color_never, color_always, color_if_tty };

static char const *const color_args[] =
{
  "always", "yes", "force",
  "never", "no", "none",
  "auto", "tty", "if-tty", nullptr
};
static color_type const color_types[] =
{
  color_always, color_always, color_always,
  color_never, color_never, color_never,
  color_if_tty, color_if_tty, color_if_tty
};
ARGMATCH_VERIFY(color_args, color_types);

static color_type color_opt = color_if_tty;
static const char* bright_red = "\033[1;31m";
static const char* bold = "\033[1m";
static const char* bold_std = "\033[0;1m";
static const char* reset_color = "\033[m";

static unsigned states = 200;
static float density = 0.1;
static const char* json_output = nullptr;
static const char* csv_output = nullptr;
static bool want_stats = false;
static bool allow_dups = false;
static bool no_checks = false;
static bool no_complement = false;
static bool determinize = false;
static bool stop_on_error = false;
static int seed = 0;
static unsigned products = 1;
static bool products_avg = true;
static bool opt_omit = false;
static const char* bogus_output_filename = nullptr;
static output_file* bogus_output = nullptr;
static output_file* grind_output = nullptr;
static bool verbose = false;
static bool ignore_exec_fail = false;
static unsigned ignored_exec_fail = 0;
static bool opt_automata = false;
static bool opt_strength = false;
static bool opt_ambiguous = false;

static bool global_error_flag = false;
static unsigned oom_count = 0U;

static std::ostream&
global_error()
{
  global_error_flag = true;
  if (color_opt)
    std::cerr << bright_red;
  return std::cerr;
}

static std::ostream&
example()
{
  if (color_opt)
    std::cerr << bold_std;
  return std::cerr;
}


static void
end_error()
{
  if (color_opt)
    std::cerr << reset_color;
}


struct statistics
{
  statistics()
    : ok(false),
      alternating(false),
      status_str(nullptr),
      status_code(0),
      time(0),
      states(0),
      edges(0),
      transitions(0),
      acc(0),
      scc(0),
      nonacc_scc(0),
      terminal_scc(0),
      weak_scc(0),
      strong_scc(0),
      nondetstates(0),
      nondeterministic(false),
      terminal_aut(false),
      weak_aut(false),
      strong_aut(false)
  {
  }

  // If OK is false, only the status_str, status_code, and time fields
  // should be valid.
  bool ok;
  bool alternating;
  const char* status_str;
  int status_code;
  double time;
  unsigned states;
  unsigned edges;
  unsigned transitions;
  unsigned acc;
  unsigned scc;
  unsigned nonacc_scc;
  unsigned terminal_scc;
  unsigned weak_scc;
  unsigned strong_scc;
  unsigned nondetstates;
  bool nondeterministic;
  bool terminal_aut;
  bool weak_aut;
  bool strong_aut;
  std::vector<double> product_states;
  std::vector<double> product_transitions;
  std::vector<double> product_scc;
  bool ambiguous;
  bool complete;
  std::string hoa_str;

  static void
  fields(std::ostream& os, bool show_exit)
  {
    if (show_exit)
      os << "\"exit_status\",\"exit_code\",";
    os << ("\"time\","
           "\"states\","
           "\"edges\","
           "\"transitions\","
           "\"acc\","
           "\"scc\",");
    if (opt_strength)
      os << ("\"nonacc_scc\","
             "\"terminal_scc\","
             "\"weak_scc\","
             "\"strong_scc\",");
    os << ("\"nondet_states\","
           "\"nondet_aut\",");
    if (opt_strength)
      os << ("\"terminal_aut\","
             "\"weak_aut\","
             "\"strong_aut\",");
    if (opt_ambiguous)
      os << "\"ambiguous_aut\",";
    os << "\"complete_aut\"";
    size_t m = products_avg ? 1U : products;
    for (size_t i = 0; i < m; ++i)
      os << ",\"product_states\",\"product_transitions\",\"product_scc\"";
    if (opt_automata)
      os << ",\"automaton\"";
  }

  void
  to_csv(std::ostream& os, bool show_exit, const char* na = "",
         bool csv_escape = true)
  {
    if (show_exit)
      os << '"' << status_str << "\"," << status_code << ',';
    os << time << ',';
    if (ok)
      {
        os << states << ','
           << edges << ','
           << transitions << ','
           << acc << ','
           << scc << ',';
        if (opt_strength)
          {
            if (alternating)
              os << ",,,,";
            else
              os << nonacc_scc << ','
                 << terminal_scc << ','
                 << weak_scc << ','
                 << strong_scc << ',';
          }
        os << nondetstates << ','
           << nondeterministic << ',';
        if (opt_strength)
          {
            if (alternating)
              os << ",,,";
            else
              os << terminal_aut << ','
                 << weak_aut << ','
                 << strong_aut << ',';
          }
        if (opt_ambiguous)
          {
            if (alternating)
              os << ',';
            else
              os << ambiguous << ',';
          }
        os << complete;
        if (!products_avg)
          {
            for (size_t i = 0; i < products; ++i)
              os << ',' << product_states[i]
                 << ',' << product_transitions[i]
                 << ',' << product_scc[i];
          }
        else
          {
            double st = 0.0;
            double tr = 0.0;
            double sc = 0.0;
            for (size_t i = 0; i < products; ++i)
              {
                st += product_states[i];
                tr += product_transitions[i];
                sc += product_scc[i];
              }
            os << ',' << (st / products)
               << ',' << (tr / products)
               << ',' << (sc / products);
          }
      }
    else
      {
        size_t m = products_avg ? 1U : products;
        m *= 3;
        m += 7;
        if (opt_strength)
          m += 7;
        if (opt_ambiguous)
          ++m;
        os << na;
        for (size_t i = 0; i < m; ++i)
          os << ',' << na;
      }
    if (opt_automata)
      {
        os << ',';
        if (hoa_str.empty())
          os << na;
        else if (csv_escape)
          spot::escape_rfc4180(os << '"', hoa_str) << '"';
        else
          spot::escape_str(os << '"', hoa_str) << '"';
      }
  }
};

typedef std::vector<statistics> statistics_formula;
typedef std::vector<statistics_formula> statistics_vector;
statistics_vector vstats;
std::vector<std::string> formulas;

static int
parse_opt(int key, char* arg, struct argp_state*)
{
  // This switch is alphabetically-ordered.
  switch (key)
    {
    case 'D':
      determinize = true;
      products = 0;
      break;
    case ARGP_KEY_ARG:
      if (arg[0] == '-' && !arg[1])
        jobs.emplace_back(arg, true);
      else
        translators.push_back(arg);
      break;
    case OPT_AUTOMATA:
      opt_automata = true;
      break;
    case OPT_BOGUS:
      {
        bogus_output = new output_file(arg);
        bogus_output_filename = arg;
        break;
      }
    case OPT_COLOR:
      {
        if (arg)
          color_opt = XARGMATCH("--color", arg, color_args, color_types);
        else
          color_opt = color_always;
        break;
      }
    case OPT_CSV:
      want_stats = true;
      csv_output = arg ? arg : "-";
      break;
    case OPT_DENSITY:
      density = to_probability(arg);
      break;
    case OPT_DUPS:
      allow_dups = true;
      break;
    case OPT_GRIND:
      grind_output = new output_file(arg);
      break;
    case OPT_IGNORE_EXEC_FAIL:
      ignore_exec_fail = true;
      break;
    case OPT_JSON:
      want_stats = true;
      json_output = arg ? arg : "-";
      break;
    case OPT_PRODUCTS:
      if (*arg == '+')
        {
          products_avg = false;
          ++arg;
        }
      products = to_pos_int(arg);
      if (products == 0)
        products_avg = false;
      break;
    case OPT_NOCHECKS:
      no_checks = true;
      no_complement = true;
      break;
    case OPT_NOCOMP:
      no_complement = true;
      break;
    case OPT_OMIT:
      opt_omit = true;
      break;
    case OPT_SEED:
      seed = to_pos_int(arg);
      break;
    case OPT_STATES:
      states = to_pos_int(arg);
      break;
    case OPT_STOP_ERR:
      stop_on_error = true;
      break;
    case OPT_STRENGTH:
      opt_strength = true;
      break;
    case OPT_AMBIGUOUS:
      opt_ambiguous = true;
      break;
    case OPT_VERBOSE:
      verbose = true;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

namespace
{
  class xtranslator_runner final: public translator_runner
  {
  public:
    xtranslator_runner(spot::bdd_dict_ptr dict)
      : translator_runner(dict)
    {
    }

    spot::twa_graph_ptr
    translate(unsigned int translator_num, char l, statistics_formula* fstats,
              bool& problem)
    {
      output.reset(translator_num);

      std::ostringstream command;
      format(command, translators[translator_num].cmd);

      std::string cmd = command.str();
      std::cerr << "Running [" << l << translator_num << "]: "
                << cmd << std::endl;
      process_timer timer;
      timer.start();
      int es = exec_with_timeout(cmd.c_str());
      timer.stop();
      const char* status_str = nullptr;

      spot::twa_graph_ptr res = nullptr;
      if (timed_out)
        {
          // This is not considered to be a global error.
          std::cerr << "warning: timeout during execution of command\n";
          ++timeout_count;
          status_str = "timeout";
          problem = false;        // A timeout is not a sign of a bug
          es = -1;
        }
      else if (WIFSIGNALED(es))
        {
          status_str = "signal";
          problem = true;
          es = WTERMSIG(es);
          global_error() << "error: execution terminated by signal "
                         << es << ".\n";
          end_error();
        }
      else if (WIFEXITED(es) && WEXITSTATUS(es) != 0)
        {
          es = WEXITSTATUS(es);
          status_str = "exit code";
          if (!ignore_exec_fail)
            {
              problem = true;
              global_error() << "error: execution returned exit code "
                             << es << ".\n";
              end_error();
            }
          else
            {
              problem = false;
              std::cerr << "warning: execution returned exit code "
                        << es << ".\n";
              ++ignored_exec_fail;
            }
        }
      else
        {
          status_str = "ok";
          problem = false;
          es = 0;

          auto aut = spot::parse_aut(output.val()->name(), dict,
                                     spot::default_environment::instance(),
                                     opt_parse);
          if (!aut->errors.empty())
            {
              status_str = "parse error";
              problem = true;
              es = -1;
              std::ostream& err = global_error();
              err << "error: failed to parse the produced automaton.\n";
              aut->format_errors(err);
              end_error();
              res = nullptr;
            }
          else if (aut->aborted)
            {
              status_str = "aborted";
              problem = true;
              es = -1;
              global_error()  << "error: aborted HOA file.\n";
              end_error();
              res = nullptr;
            }
          else
            {
              res = aut->aut;
            }
        }

      if (want_stats)
        {
          statistics* st = &(*fstats)[translator_num];
          st->status_str = status_str;
          st->status_code = es;
          st->time = timer.get_lap_sw();

          // Compute statistics.
          if (res)
            {
              if (verbose)
                std::cerr << "info: getting statistics\n";
              st->ok = true;
              st->alternating = !res->is_existential();
              spot::twa_sub_statistics s = sub_stats_reachable(res);
              st->states = s.states;
              st->edges = s.edges;
              st->transitions = s.transitions;
              st->acc = res->acc().num_sets();
              spot::scc_info m(res);
              unsigned c = m.scc_count();
              st->scc = c;
              st->nondetstates = spot::count_nondet_states(res);
              st->nondeterministic = st->nondetstates != 0;
              if (opt_strength && !st->alternating)
                {
                  m.determine_unknown_acceptance();
                  for (unsigned n = 0; n < c; ++n)
                    {
                      if (m.is_rejecting_scc(n))
                        ++st->nonacc_scc;
                      else if (is_terminal_scc(m, n))
                        ++st->terminal_scc;
                      else if (is_weak_scc(m, n))
                        ++st->weak_scc;
                      else
                        ++st->strong_scc;
                    }
                  if (st->strong_scc)
                    st->strong_aut = true;
                  else if (st->weak_scc)
                    st->weak_aut = true;
                  else
                    st->terminal_aut = true;
                }
              if (opt_ambiguous && !st->alternating)
                st->ambiguous = !spot::is_unambiguous(res);
              st->complete = spot::is_complete(res);

              if (opt_automata)
                {
                  std::ostringstream os;
                  spot::print_hoa(os, res, "l");
                  st->hoa_str = os.str();
                }
            }
        }
      output.cleanup();
      return res;
    }
  };

  static bool
  check_empty_prod(const spot::const_twa_graph_ptr& aut_i,
                   const spot::const_twa_graph_ptr& aut_j,
                   size_t i, size_t j, bool icomp, bool jcomp)
  {
    if (aut_i->num_sets() + aut_j->num_sets()
        > 8 * sizeof(spot::acc_cond::mark_t::value_t))
      {
        // Report the skipped test if both automata are not
        // complemented, or the --verbose option is used,
        if (!verbose && (icomp || jcomp))
          return false;
        std::cerr << "info: building ";
        if (icomp)
          std::cerr << "Comp(N" << i << ')';
        else
          std::cerr << 'P' << i;
        if (jcomp)
          std::cerr << "*Comp(P" << j << ')';
        else
          std::cerr << "*N" << j;
        std::cerr << " requires more acceptance sets than supported\n";
        return false;
      }

    auto prod = spot::product(aut_i, aut_j);

    if (verbose)
      {
        std::cerr << "info: check_empty ";
        if (icomp)
          std::cerr << "Comp(N" << i << ')';
        else
          std::cerr << 'P' << i;
        if (jcomp)
          std::cerr << "*Comp(P" << j << ')';
        else
          std::cerr << "*N" << j;
        std::cerr << '\n';
      }

    auto w = prod->accepting_word();
    if (w)
      {
        std::ostream& err = global_error();
        err << "error: ";
        if (icomp)
          err << "Comp(N" << i << ')';
        else
          err << 'P' << i;
        if (jcomp)
          err << "*Comp(P" << j << ')';
        else
          err << "*N" << j;
        err << " is nonempty; both automata accept the infinite word:\n"
            << "       ";
        example() << *w << '\n';
        end_error();
      }
    return !!w;
  }

  static bool
  cross_check(const std::vector<spot::scc_info*>& maps, char l, unsigned p)
  {
    size_t m = maps.size();
    if (verbose)
      {
        std::cerr << "info: cross_check {";
        bool first = true;
        for (size_t i = 0; i < m; ++i)
          if (maps[i])
            {
              if (first)
                first = false;
              else
                std::cerr << ',';
              std::cerr << l << i;
            }
        std::cerr << "}, state-space #" << p << '/' << products << '\n';
      }

    std::vector<bool> res(m);
    unsigned verified = 0;
    unsigned violated = 0;
    for (size_t i = 0; i < m; ++i)
      if (spot::scc_info* m = maps[i])
        {
          // r == true iff the automaton i is accepting.
          bool r = false;
          for (auto& scc: *m)
            if (scc.is_accepting())
              {
                r = true;
                break;
              }
          res[i] = r;
          if (r)
            ++verified;
          else
            ++violated;
        }
    if (verified != 0 && violated != 0)
      {
        std::ostream& err = global_error();
        err << "error: {";
        bool first = true;
        for (size_t i = 0; i < m; ++i)
          if (maps[i] && res[i])
            {
              if (first)
                first = false;
              else
                err << ',';
              err << l << i;
            }
        err << "} disagree with {";
        std::ostringstream os;
        first = true;
        for (size_t i = 0; i < m; ++i)
          if (maps[i] && !res[i])
            {
              if (first)
                first = false;
              else
                os << ',';
              os << l << i;
            }
        err << os.str() << "} when evaluating ";
        if (products > 1)
          err << "state-space #" << p << '/' << products << '\n';
        else
          err << "the state-space\n";
        err << "       the following word(s) are not accepted by {"
            << os.str() << "}:\n";
        for (size_t i = 0; i < m; ++i)
          if (maps[i] && res[i])
            {
              global_error() << "  " << l << i << " accepts: ";
              example() << *maps[i]->get_aut()->accepting_word() << '\n';
            }
        end_error();
        return true;
      }
    return false;
  }

  typedef std::set<unsigned> state_set;

  // Collect all the states of SSPACE that appear in the accepting SCCs
  // of PROD.  (Trivial SCCs are considered accepting.)
  static void
  states_in_acc(const spot::scc_info* m, state_set& s)
  {
    auto aut = m->get_aut();
    auto ps = aut->get_named_prop<const spot::product_states>("product-states");
    for (auto& scc: *m)
      if (scc.is_accepting() || scc.is_trivial())
        for (auto i: scc.states())
          // Get the projection on sspace.
          s.insert((*ps)[i].second);
  }

  static bool
  consistency_check(const spot::scc_info* pos, const spot::scc_info* neg)
  {
    // the states of SSPACE should appear in the accepting SCC of at
    // least one of POS or NEG.  Maybe both.
    state_set s;
    states_in_acc(pos, s);
    states_in_acc(neg, s);
    return s.size() == states;
  }

  typedef
  std::unordered_set<spot::formula> fset_t;

  class processor final: public job_processor
  {
    spot::bdd_dict_ptr dict = spot::make_bdd_dict();
    xtranslator_runner runner;
    fset_t unique_set;
  public:
    processor():
      runner(dict)
    {
    }

    int
    process_string(const std::string& input,
                   const char* filename,
                   int linenum) override
    {
      auto pf = parse_formula(input);
      if (!pf.f || !pf.errors.empty())
        {
          if (filename)
            error_at_line(0, 0, filename, linenum, "parse error:");
          pf.format_errors(std::cerr);
          return 1;
        }
      auto f = pf.f;

      int res = process_formula(f, filename, linenum);

      if (res && bogus_output)
        bogus_output->ostream() << input << std::endl;
      if (res && grind_output)
        {
          std::string bogus = input;
          std::vector<spot::formula> mutations;
          unsigned mutation_count;
          unsigned mutation_max;
          while        (res)
            {
              std::cerr << "Trying to find a bogus mutation of ";
              if (color_opt)
                std::cerr << bold;
              std::cerr << bogus;
              if (color_opt)
                std::cerr << reset_color;
              std::cerr << "...\n";

              mutations = mutate(f);
              mutation_count = 1;
              mutation_max = mutations.size();
              res = 0;
              for (auto g: mutations)
                {
                  std::cerr << "Mutation " << mutation_count << '/'
                            << mutation_max << ": ";
                  f = g;
                  res = process_formula(g);
                  if (res)
                    break;
                  ++mutation_count;
                }
              if (res)
                {
                  if (lbt_input)
                    bogus = spot::str_lbt_ltl(f);
                  else
                    bogus = spot::str_psl(f);
                  if (bogus_output)
                    bogus_output->ostream() << bogus << std::endl;
                }
            }
          std::cerr << "Smallest bogus mutation found for ";
          if (color_opt)
            std::cerr << bold;
          std::cerr << input;
          if (color_opt)
            std::cerr << reset_color;
          std::cerr << " is ";
          if (color_opt)
            std::cerr << bold;
          std::cerr << bogus;
          if (color_opt)
            std::cerr << reset_color;
          std::cerr << ".\n\n";
          grind_output->ostream() << bogus << std::endl;
        }
      return 0;
    }

    void product_stats(statistics_formula* stats, unsigned i,
                        spot::scc_info* sm)
    {
      if (verbose && sm)
        std::cerr << "info:               " << sm->scc_count()
                  << " SCCs\n";
      // Statistics
      if (want_stats)
        {
          if (sm)
            {
              (*stats)[i].product_scc.push_back(sm->scc_count());
              spot::twa_statistics s = spot::stats_reachable(sm->get_aut());
              (*stats)[i].product_states.push_back(s.states);
              (*stats)[i].product_transitions.push_back(s.edges);
            }
          else
            {
              double n = nan("");
              (*stats)[i].product_scc.push_back(n);
              (*stats)[i].product_states.push_back(n);
              (*stats)[i].product_transitions.push_back(n);
            }
        }
    }

    int
    process_formula(spot::formula f,
                    const char* filename = nullptr, int linenum = 0) override
    {
      static unsigned round = 0;

      if (opt_relabel
          // If we need LBT atomic proposition in any of the input or
          // output, relabel the formula.
          ||  (!f.has_lbt_atomic_props() &&
               (runner.has('l') || runner.has('L') || runner.has('T')))
          // Likewise for Spin
          || (!f.has_spin_atomic_props() &&
              (runner.has('s') || runner.has('S'))))
        f = spot::relabel(f, spot::Pnn);

      // ---------- Positive Formula ----------

      runner.round_formula(f, round);

      // Call formula() before printing anything else, in case it
      // complains.
      std::string fstr = runner.formula();
      if (filename)
        std::cerr << filename << ':';
      if (linenum)
        std::cerr << linenum << ':';
      if (filename || linenum)
        std::cerr << ' ';
      if (color_opt)
        std::cerr << bold;
      std::cerr << fstr << '\n';
      if (color_opt)
        std::cerr << reset_color;

      // Make sure we do not translate the same formula twice.
      if (!allow_dups)
        {
          if (!unique_set.insert(f).second)
            {
              std::cerr
                << ("warning: This formula or its negation has already"
                    " been checked.\n         Use --allow-dups if it "
                    "should not be ignored.\n")
                << std::endl;
              return 0;
            }
        }


      int problems = 0;

      // These store the result of the translation of the positive and
      // negative formulas.
      size_t m = translators.size();
      std::vector<spot::twa_graph_ptr> pos(m);
      std::vector<spot::twa_graph_ptr> neg(m);
      // These store the complement of the above results, when we can
      // compute it easily.
      std::vector<spot::twa_graph_ptr> comp_pos(m);
      std::vector<spot::twa_graph_ptr> comp_neg(m);


      unsigned n = vstats.size();
      vstats.resize(n + (no_checks ? 1 : 2));
      statistics_formula* pstats = &vstats[n];
      statistics_formula* nstats = nullptr;
      pstats->resize(m);
      formulas.push_back(fstr);

      for (size_t n = 0; n < m; ++n)
        {
          bool prob;
          pos[n] = runner.translate(n, 'P', pstats, prob);
          problems += prob;
        }

      // ---------- Negative Formula ----------

      // The negative formula is only needed when checks are
      // activated.
      if (!no_checks)
        {
          nstats = &vstats[n + 1];
          nstats->resize(m);

          spot::formula nf = spot::formula::Not(f);

          if (!allow_dups)
            {
              bool res = unique_set.insert(nf).second;
              // It is not possible to discover that nf has already been
              // translated, otherwise that would mean that f had been
              // translated too and we would have caught it before.
              assert(res);
              (void) res;
            }

          runner.round_formula(nf, round);
          formulas.push_back(runner.formula());

          for (size_t n = 0; n < m; ++n)
            {
              bool prob;
              neg[n] = runner.translate(n, 'N', nstats, prob);
              problems += prob;
            }
        }

      spot::cleanup_tmpfiles();
      ++round;

      auto printsize = [](const spot::const_twa_graph_ptr& aut)
        {
          std::cerr << aut->num_states() << " st.,"
          << aut->num_edges() << " ed.,"
          << aut->num_sets() << " sets";
        };

      if (verbose)
        {
          std::cerr << "info: collected automata:\n";
          auto tmp = [&](std::vector<spot::twa_graph_ptr>& x, unsigned i,
                         const char prefix)
            {
              if (!x[i])
                return;
              std::cerr << "info:   " << prefix << i << "\t(";
              printsize(x[i]);
              std::cerr << ')';
              if (!x[i]->is_existential())
                std::cerr << " univ-edges";
              if (is_deterministic(x[i]))
                std::cerr << " deterministic";
              if (is_complete(x[i]))
                std::cerr << " complete";
              std::cerr << '\n';
            };
          for (unsigned i = 0; i < m; ++i)
            {
              tmp(pos, i, 'P');
              tmp(neg, i, 'N');
            }
        }

      if (!no_checks)
        {
          std::cerr << "Performing sanity checks and gathering statistics..."
                    << std::endl;

          bool print_first = verbose;
          auto unalt = [&](std::vector<spot::twa_graph_ptr>& x,
                           unsigned i, char prefix)
            {
              if (!x[i] || x[i]->is_existential())
                return;
              if (verbose)
                {
                  if (print_first)
                    {
                      std::cerr <<
                        "info: getting rid of universal edges...\n";
                      print_first = false;
                    }
                  std::cerr << "info:   " << prefix << i << "\t(";
                  printsize(x[i]);
                  std::cerr << ") ->";
                }
              x[i] = remove_alternation(x[i]);
              if (verbose)
                {
                  std::cerr << " (";
                  printsize(x[i]);
                  std::cerr << ")\n";
                }
            };
          auto complement = [&](const std::vector<spot::twa_graph_ptr>& x,
                                std::vector<spot::twa_graph_ptr>& comp,
                                unsigned i)
            {
              if (!no_complement && x[i] && is_deterministic(x[i]))
                comp[i] = dualize(x[i]);
            };

          for (unsigned i = 0; i < m; ++i)
            {
              unalt(pos, i, 'P');
              complement(pos, comp_pos, i);
              unalt(neg, i, 'N');
              complement(neg, comp_neg, i);
            }

          if (determinize && !no_complement)
            {
              print_first = verbose;
              auto tmp = [&](std::vector<spot::twa_graph_ptr>& from,
                             std::vector<spot::twa_graph_ptr>& to, unsigned i,
                             char prefix)
                {
                  if (from[i] && !to[i])
                    {
                      if (print_first)
                        {
                          std::cerr << "info: complementing non-deterministic "
                            "automata via determinization...\n";
                          print_first = false;
                        }
                      spot::postprocessor p;
                      p.set_type(spot::postprocessor::Generic);
                      p.set_pref(spot::postprocessor::Deterministic);
                      p.set_level(spot::postprocessor::Low);
                      to[i] = dualize(p.run(from[i]));
                      if (verbose)
                        {
                          std::cerr << "info:   " << prefix << i << "\t(";
                          printsize(from[i]);
                          std::cerr << ") -> (";
                          printsize(to[i]);
                          std::cerr << ")\tComp(" << prefix << i << ")\n";
                        }
                    }
                };
              for (unsigned i = 0; i < m; ++i)
                {
                  tmp(pos, comp_pos, i, 'P');
                  tmp(neg, comp_neg, i, 'N');
                }
            }

          print_first = verbose;
          auto tmp = [&](std::vector<spot::twa_graph_ptr>& x, unsigned i,
                         const char* prefix, const char* suffix)
            {
              if (!x[i])
                return;
              if (x[i]->acc().uses_fin_acceptance())
                {
                  if (verbose)
                    {
                      if (print_first)
                        {
                          std::cerr <<
                            "info: getting rid of any Fin acceptance...\n";
                          print_first = false;
                        }
                      std::cerr << "info:\t" << prefix << i
                                << suffix << "\t(";
                      printsize(x[i]);
                      std::cerr << ") ->";
                    }
                  cleanup_acceptance_here(x[i]);
                  x[i] = remove_fin(x[i]);
                  if (verbose)
                    {
                      std::cerr << " (";
                      printsize(x[i]);
                      std::cerr << ")\n";
                    }
                }
              else
                {
                  // Remove useless sets nonetheless.
                  cleanup_acceptance_here(x[i]);
                }
            };
          for (unsigned i = 0; i < m; ++i)
            {
              tmp(pos, i, "     P", " ");
              tmp(neg, i, "     N", " ");
              tmp(comp_pos, i, "Comp(P", ")");
              tmp(comp_neg, i, "Comp(N", ")");
            }

          // intersection test
          for (size_t i = 0; i < m; ++i)
            if (pos[i])
              for (size_t j = 0; j < m; ++j)
                if (neg[j])
                  {
                    problems +=
                      check_empty_prod(pos[i], neg[j], i, j, false, false);

                    // Deal with the extra complemented automata if we
                    // have some.

                    // If comp_pos[j] and comp_neg[j] exist for the
                    // same j, it means pos[j] and neg[j] were both
                    // deterministic.  In that case, we will want to
                    // make sure that comp_pos[j]*comp_neg[j] is empty
                    // to assert the complementary of pos[j] and
                    // neg[j].  However using comp_pos[j] and
                    // comp_neg[j] against other translator will not
                    // give us any more insight than pos[j] and
                    // neg[j].  So we only do intersection checks with
                    // a complement automata when one of the two
                    // translation was not deterministic.

                    if (i != j && comp_pos[j] && !comp_neg[j])
                      problems +=
                        check_empty_prod(pos[i], comp_pos[j],
                                         i, j, false, true);
                    if (i != j && comp_neg[i] && !comp_pos[i])
                      problems +=
                        check_empty_prod(comp_neg[i], neg[j],
                                         i, j, true, false);
                    if (comp_pos[i] && comp_neg[j] &&
                        (i == j || (!comp_neg[i] && !comp_pos[j])))
                      problems +=
                        check_empty_prod(comp_neg[j], comp_pos[i],
                                         j, i, true, true);
                  }
        }
      else
        {
          std::cerr << "Gathering statistics..." << std::endl;
        }

      spot::atomic_prop_set* ap = spot::atomic_prop_collect(f);

      if (want_stats)
        for (size_t i = 0; i < m; ++i)
          {
            (*pstats)[i].product_states.reserve(products);
            (*pstats)[i].product_transitions.reserve(products);
            (*pstats)[i].product_scc.reserve(products);
            if (neg[i])
              {
                (*nstats)[i].product_states.reserve(products);
                (*nstats)[i].product_transitions.reserve(products);
                (*nstats)[i].product_scc.reserve(products);
              }
          }
      for (unsigned p = 0; p < products; ++p)
        {
          // build a random state-space.
          spot::srand(seed);

          if (verbose)
            std::cerr << "info: building state-space #" << p << '/' << products
                      << " of " << states  << " states with seed " << seed
                      << '\n';

          auto statespace = spot::random_graph(states, density, ap, dict);

          if (verbose)
            std::cerr << "info: state-space has "
                      << statespace->num_edges()
                      << " edges\n";

          // Associated SCC maps.
          std::vector<spot::scc_info*> pos_map(m);
          std::vector<spot::scc_info*> neg_map(m);
          for (size_t i = 0; i < m; ++i)
            if (pos[i])
              {
                if (verbose)
                  std::cerr << ("info: building product between state-space and"
                                " P") << i
                            << " (" << pos[i]->num_states() << " st., "
                            << pos[i]->num_edges() << " ed.)\n";

                spot::scc_info* sm = nullptr;
                try
                  {
                    auto p = spot::product(pos[i], statespace);
                    if (verbose)
                      std::cerr << "info:   product has " << p->num_states()
                                << " st., " << p->num_edges()
                                << " ed.\n";
                    sm = new spot::scc_info(p);
                  }
                catch (std::bad_alloc&)
                  {
                    std::cerr << ("warning: not enough memory to build "
                                  "product of P") << i << " with state-space";
                    if (products > 1)
                      std::cerr << " #" << p << '/' << products << '\n';
                    std::cerr << '\n';
                    ++oom_count;
                  }
                pos_map[i] = sm;
                product_stats(pstats, i, sm);
              }

          if (!no_checks)
            for (size_t i = 0; i < m; ++i)
              if (neg[i])
                {
                  if (verbose)
                    std::cerr << ("info: building product between state-space"
                                  " and N") << i
                              << " (" << neg[i]->num_states() << " st., "
                              << neg[i]->num_edges() << " ed.)\n";

                  spot::scc_info* sm = nullptr;
                  try
                    {
                      auto p = spot::product(neg[i], statespace);
                      if (verbose)
                        std::cerr << "info:   product has " << p->num_states()
                                  << " st., " << p->num_edges()
                                  << " ed.\n";
                      sm = new spot::scc_info(p);
                    }
                  catch (std::bad_alloc&)
                    {
                      std::cerr << ("warning: not enough memory to build "
                                    "product of N") << i << " with state-space";
                      if (products > 1)
                        std::cerr << " #" << p << '/' << products << '\n';
                      std::cerr << '\n';
                      ++oom_count;
                    }

                  neg_map[i] = sm;
                  product_stats(nstats, i, sm);
                }

          if (!no_checks)
            {
              // cross-comparison test
              problems += cross_check(pos_map, 'P', p);
              problems += cross_check(neg_map, 'N', p);

              // consistency check
              for (size_t i = 0; i < m; ++i)
                if (pos_map[i] && neg_map[i])
                  {
                    if (verbose)
                      std::cerr << "info: consistency_check (P" << i
                                << ",N" << i << "), state-space #"
                                << p << '/' << products << '\n';
                    if (!(consistency_check(pos_map[i], neg_map[i])))
                      {
                        ++problems;

                        std::ostream& err = global_error();
                        err << "error: inconsistency between P" << i
                            << " and N" << i;
                        if (products > 1)
                          err << " for state-space #" << p
                              << '/' << products << '\n';
                        else
                          err << '\n';
                        end_error();
                      }
                  }
            }

          // Cleanup.
          if (!no_checks)
            for (size_t i = 0; i < m; ++i)
              delete neg_map[i];
          for (size_t i = 0; i < m; ++i)
            delete pos_map[i];
          ++seed;
        }
      std::cerr << std::endl;
      delete ap;

      // Shall we stop processing formulas now?
      abort_run = global_error_flag && stop_on_error;
      return problems;
    }
  };
}

// Output an RFC4180-compatible CSV file.
static void
print_stats_csv(const char* filename)
{
  if (verbose)
    std::cerr << "info: writing CSV to " << filename << '\n';

  output_file outf(filename);
  std::ostream& out = outf.ostream();

  unsigned ntrans = translators.size();
  unsigned rounds = vstats.size();
  assert(rounds == formulas.size());

  if (!outf.append())
    {
      // Do not output the header line if we append to a file.
      // (Even if that file was empty initially.)
      out << "\"formula\",\"tool\",";
      statistics::fields(out, !opt_omit);
      out << '\n';
    }
  for (unsigned r = 0; r < rounds; ++r)
    for (unsigned t = 0; t < ntrans; ++t)
      if (!opt_omit || vstats[r][t].ok)
        {
          out << '"';
          spot::escape_rfc4180(out, formulas[r]);
          out << "\",\"";
          spot::escape_rfc4180(out, translators[t].name);
          out << "\",";
          vstats[r][t].to_csv(out, !opt_omit);
          out << '\n';
        }
  outf.close(filename);
}

static void
print_stats_json(const char* filename)
{
  if (verbose)
    std::cerr << "info: writing JSON to " << filename << '\n';

  output_file outf(filename);
  std::ostream& out = outf.ostream();

  unsigned ntrans = translators.size();
  unsigned rounds = vstats.size();
  assert(rounds == formulas.size());

  out << "{\n  \"tool\": [\n    \"";
  spot::escape_str(out, translators[0].name);
  for (unsigned t = 1; t < ntrans; ++t)
    {
      out << "\",\n    \"";
      spot::escape_str(out, translators[t].name);
    }
  out << "\"\n  ],\n  \"formula\": [\n    \"";
  spot::escape_str(out, formulas[0]);
  for (unsigned r = 1; r < rounds; ++r)
    {
      out << "\",\n    \"";
      spot::escape_str(out, formulas[r]);
    }
  out << ("\"\n  ],\n  \"fields\":  [\n  \"formula\",\"tool\",");
  statistics::fields(out, !opt_omit);
  out << "\n  ],\n  \"inputs\":  [ 0, 1 ],";
  out << "\n  \"results\": [";
  bool notfirst = false;
  for (unsigned r = 0; r < rounds; ++r)
    for (unsigned t = 0; t < ntrans; ++t)
      if (!opt_omit || vstats[r][t].ok)
        {
          if (notfirst)
            out << ',';
          notfirst = true;
          out << "\n    [ " << r << ',' << t << ',';
          vstats[r][t].to_csv(out, !opt_omit, "null", false);
          out << " ]";
        }
  out << "\n  ]\n}\n";
  outf.close(filename);
}

int
main(int argc, char** argv)
{
  setup(argv);

  const argp ap = { options, parse_opt, "[COMMANDFMT...]",
                    argp_program_doc, children, nullptr, nullptr };

  if (int err = argp_parse(&ap, argc, argv, ARGP_NO_HELP, nullptr, nullptr))
    exit(err);

  check_no_formula();

  if (translators.empty())
    error(2, 0, "No translator to run?  Run '%s --help' for usage.",
          program_name);

  if (color_opt == color_if_tty)
    color_opt = isatty(STDERR_FILENO) ? color_always : color_never;

  setup_sig_handler();

  processor p;
  if (p.run())
    return 2;

  if (formulas.empty())
    {
      error(2, 0, "no formula to translate");
    }
  else
    {
      if (global_error_flag)
        {
          std::ostream& err = global_error();
          if (bogus_output)
            err << ("error: some error was detected during the above runs.\n"
                    "       Check file ")
                << bogus_output_filename
                << " for problematic formulas.";
          else
            err << ("error: some error was detected during the above runs,\n"
                    "       please search for 'error:' messages in the above"
                    " trace.");
          err << std::endl;
          end_error();
        }
      else if (timeout_count == 0 && ignored_exec_fail == 0 && oom_count == 0)
        {
          std::cerr << "No problem detected." << std::endl;
        }
      else
        {
          std::cerr << "No major problem detected." << std::endl;
        }

      unsigned additional_errors = 0U;
      additional_errors += timeout_count > 0;
      additional_errors += ignored_exec_fail > 0;
      additional_errors += oom_count > 0;
      if (additional_errors)
        {
          std::cerr << (global_error_flag ? "Additionally, " : "However, ");
          if (timeout_count)
            {
              if (additional_errors > 1)
                std::cerr << "\n  - ";
              if (timeout_count == 1)
                std::cerr << "1 timeout occurred";
              else
                std::cerr << timeout_count << " timeouts occurred";
            }

          if (oom_count)
            {
              if (additional_errors > 1)
                std::cerr << "\n  - ";
              if (oom_count == 1)
                std::cerr << "1 state-space product was";
              else
                std::cerr << oom_count << "state-space products were";
              std::cerr << " skipped by lack of memory";
            }

          if (ignored_exec_fail)
            {
              if (additional_errors > 1)
                std::cerr << "\n  - ";
              if (ignored_exec_fail == 1)
                std::cerr << "1 non-zero exit status was ignored";
              else
                std::cerr << ignored_exec_fail
                          << " non-zero exit statuses were ignored";
            }
          if (additional_errors == 1)
            std::cerr << '.';
          std::cerr << std::endl;
        }
    }

  delete bogus_output;
  delete grind_output;

  if (json_output)
    print_stats_json(json_output);
  if (csv_output)
    print_stats_csv(csv_output);

  return global_error_flag;
}
