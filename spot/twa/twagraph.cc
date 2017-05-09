// -*- coding: utf-8 -*-
// Copyright (C) 2014, 2015, 2016, 2017 Laboratoire de Recherche et
// Développement de l'Epita.
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

#include <spot/twa/twagraph.hh>
#include <spot/tl/print.hh>

namespace spot
{
  void
  twa_graph::release_formula_namer(namer<formula>* namer,
                                   bool keep_names)
  {
    if (keep_names)
      {
        auto v = new std::vector<std::string>(num_states());
        auto& n = namer->names();
        unsigned ns = n.size();
        assert(n.size() <= v->size());
        for (unsigned i = 0; i < ns; ++i)
          {
            auto f = n[i];
            if (f)
              (*v)[i] = str_psl(f);
          }
        set_named_prop("state-names", v);
      }
    delete namer;
  }

    /// \brief Merge universal destinations
    ///
    /// If several states have the same universal destination, merge
    /// them all.  Also remove unused destination, and any redundant
    /// state in each destination.
  void twa_graph::merge_univ_dests()
  {
    auto& g = get_graph();
    auto& dests = g.dests_vector();
    auto& edges = g.edge_vector();

    std::vector<unsigned> old_dests;
    std::swap(dests, old_dests);
    std::vector<unsigned> seen(old_dests.size(), -1U);
    internal::univ_dest_mapper<twa_graph::graph_t> uniq(g);

    auto fixup = [&](unsigned& in_dst)
      {
        unsigned dst = in_dst;
        if ((int) dst >= 0)       // not a universal edge
          return;
        dst = ~dst;
        unsigned& nd = seen[dst];
        if (nd == -1U)
          nd = uniq.new_univ_dests(old_dests.data() + dst + 1,
                                   old_dests.data() + dst + 1 + old_dests[dst]);
        in_dst = nd;
      };

    unsigned tend = edges.size();
    for (unsigned t = 1; t < tend; t++)
      {
        if (g.is_dead_edge(t))
          continue;
        fixup(edges[t].dst);
      }
    fixup(init_number_);
  }

  void twa_graph::merge_edges()
  {
    set_named_prop("highlight-edges", nullptr);
    g_.remove_dead_edges_();
    if (!is_existential())
      merge_univ_dests();

    typedef graph_t::edge_storage_t tr_t;
    g_.sort_edges_([](const tr_t& lhs, const tr_t& rhs)
                   {
                     if (lhs.src < rhs.src)
                       return true;
                     if (lhs.src > rhs.src)
                       return false;
                     if (lhs.dst < rhs.dst)
                       return true;
                     if (lhs.dst > rhs.dst)
                       return false;
                     return lhs.acc < rhs.acc;
                     // Do not sort on conditions, we'll merge
                     // them.
                   });

    auto& trans = this->edge_vector();
    unsigned tend = trans.size();
    unsigned out = 0;
    unsigned in = 1;
    // Skip any leading false edge.
    while (in < tend && trans[in].cond == bddfalse)
      ++in;
    if (in < tend)
      {
        ++out;
        if (out != in)
          trans[out] = trans[in];
        for (++in; in < tend; ++in)
          {
            if (trans[in].cond == bddfalse) // Unusable edge
              continue;
            // Merge edges with the same source, destination, and
            // acceptance.  (We test the source last, because this is the
            // most likely test to be true as edges are ordered by
            // sources and then destinations.)
            if (trans[out].dst == trans[in].dst
                && trans[out].acc == trans[in].acc
                && trans[out].src == trans[in].src)
              {
                trans[out].cond |= trans[in].cond;
              }
            else
              {
                ++out;
                if (in != out)
                  trans[out] = trans[in];
              }
          }
      }
    if (++out != tend)
      trans.resize(out);

    tend = out;
    out = in = 2;

    // FIXME: We could should also merge edges when using
    // fin_acceptance, but the rule for Fin sets are different than
    // those for Inf sets, (and we need to be careful if a set is used
    // both as Inf and Fin)
    if ((in < tend) && !acc().uses_fin_acceptance())
      {
        typedef graph_t::edge_storage_t tr_t;
        g_.sort_edges_([](const tr_t& lhs, const tr_t& rhs)
                       {
                         if (lhs.src < rhs.src)
                           return true;
                         if (lhs.src > rhs.src)
                           return false;
                         if (lhs.dst < rhs.dst)
                           return true;
                         if (lhs.dst > rhs.dst)
                           return false;
                         return lhs.cond.id() < rhs.cond.id();
                         // Do not sort on acceptance, we'll merge
                         // them.
                       });

        for (; in < tend; ++in)
          {
            // Merge edges with the same source, destination,
            // and conditions.  (We test the source last, for the
            // same reason as above.)
            if (trans[out].dst == trans[in].dst
                && trans[out].cond.id() == trans[in].cond.id()
                && trans[out].src == trans[in].src)
              {
                trans[out].acc |= trans[in].acc;
              }
            else
              {
                ++out;
                if (in != out)
                  trans[out] = trans[in];
              }
          }
        if (++out != tend)
          trans.resize(out);
      }

    g_.chain_edges_();
  }

  void twa_graph::purge_unreachable_states()
  {
    unsigned num_states = g_.num_states();
    // The TODO vector serves two purposes:
    // - it is a stack of state to process,
    // - it is a set of processed states.
    // The lower 31 bits of each entry is a state on the stack. (The
    // next usable entry on the stack is indicated by todo_pos.)  The
    // 32th bit (i.e., the sign bit) of todo[x] indicates whether
    // states number x has been seen.
    std::vector<unsigned> todo(num_states, 0);
    const unsigned seen = 1 << (sizeof(unsigned)*8-1);
    const unsigned mask = seen - 1;
    unsigned todo_pos = 0;
    for (unsigned i: univ_dests(get_init_state_number()))
      {
        todo[i] |= seen;
        todo[todo_pos++] |= i;
      }
    do
      {
        unsigned cur = todo[--todo_pos] & mask;
        todo[todo_pos] ^= cur;        // Zero the state
        for (auto& t: g_.out(cur))
          for (unsigned dst: univ_dests(t.dst))
            if (!(todo[dst] & seen))
              {
                todo[dst] |= seen;
                todo[todo_pos++] |= dst;
              }
      }
    while (todo_pos > 0);
    // Now renumber each used state.
    unsigned current = 0;
    for (auto& v: todo)
      if (!(v & seen))
        v = -1U;
      else
        v = current++;
    if (current == todo.size())
      return;                        // No unreachable state.

    // Removing some non-deterministic dead state could make the
    // automaton universal.
    if (prop_universal().is_false())
      prop_universal(trival::maybe());
    if (prop_complete().is_false())
      prop_complete(trival::maybe());

    defrag_states(std::move(todo), current);
  }

  void twa_graph::purge_dead_states()
  {
    unsigned num_states = g_.num_states();
    std::vector<unsigned> useful(num_states, 0);

    // Make a DFS to compute a topological order.
    std::vector<unsigned> order;
    order.reserve(num_states);

    bool purge_unreachable_needed = false;

    if (is_existential())
      {
        std::vector<std::pair<unsigned, unsigned>> todo; // state, edge
        useful[get_init_state_number()] = 1;
        todo.emplace_back(init_number_, g_.state_storage(init_number_).succ);
        do
          {
            unsigned src;
            unsigned tid;
            std::tie(src, tid) = todo.back();
            if (tid == 0U)
              {
                todo.pop_back();
                order.emplace_back(src);
                continue;
              }
            auto& t = g_.edge_storage(tid);
            todo.back().second = t.next_succ;
            unsigned dst = t.dst;
            if (useful[dst] != 1)
              {
                todo.emplace_back(dst, g_.state_storage(dst).succ);
                useful[dst] = 1;
              }
          }
        while (!todo.empty());
      }
    else
      {
        // state, edge, begin, end
        std::vector<std::tuple<unsigned, unsigned,
                               const unsigned*, const unsigned*>> todo;
        auto& dests = g_.dests_vector();

        auto beginend = [&](const unsigned& dst,
                            const unsigned*& begin, const unsigned*& end)
          {
            if ((int)dst < 0)
              {
                begin = dests.data() + ~dst + 1;
                end = begin + dests[~dst];
              }
            else
              {
                begin = &dst;
                end = begin + 1;
              }
          };
        {
          const unsigned* begin;
          const unsigned* end;
          beginend(init_number_, begin, end);
          todo.emplace_back(init_number_, 0U, begin, end);
        }

        for (;;)
          {
            unsigned& tid = std::get<1>(todo.back());
            const unsigned*& begin = std::get<2>(todo.back());
            const unsigned*& end = std::get<3>(todo.back());
            if (tid == 0U && begin == end)
              {
                unsigned src = std::get<0>(todo.back());
                todo.pop_back();
                // Last transition from a state?
                if ((int)src >= 0 && (todo.empty()
                                      || src != std::get<0>(todo.back())))
                  order.emplace_back(src);
                if (todo.empty())
                  break;
                else
                  continue;
              }
            unsigned dst = *begin++;
            if (begin == end)
              {
                if (tid != 0)
                  tid = g_.edge_storage(tid).next_succ;
                if (tid != 0)
                  beginend(g_.edge_storage(tid).dst, begin, end);
              }
            if (useful[dst] != 1)
              {
                auto& ss = g_.state_storage(dst);
                unsigned succ = ss.succ;
                if (succ == 0U)
                  continue;
                useful[dst] = 1;
                const unsigned* begin;
                const unsigned* end;
                beginend(g_.edge_storage(succ).dst, begin, end);
                todo.emplace_back(dst, succ, begin, end);
              }
          }
      }

    // At this point, all reachable states with successors are marked
    // as useful.
    for (;;)
      {
        bool univ_edge_erased = false;
        // Process states in topological order to mark those without
        // successors as useless.
        for (auto s: order)
          {
            auto t = g_.out_iteraser(s);
            bool useless = true;
            while (t)
              {
                // An edge is useful if all its
                // destinations are useful.
                bool usefuledge = true;
                for (unsigned d: univ_dests(t->dst))
                  if (!useful[d])
                    {
                      usefuledge = false;
                      break;
                    }
                // Erase any useless edge
                if (!usefuledge)
                  {
                    if (is_univ_dest(t->dst))
                      univ_edge_erased = true;
                    t.erase();
                    continue;
                  }
                // if we have a edge to a useful state, then the
                // state is useful.
                useless = false;
                ++t;
              }
            if (useless)
              useful[s] = 0;
          }
        // If we have erased any universal destination, it is possible
        // that we have have created some new dead states, so we
        // actually need to redo the whole thing again until there is
        // no more universal edge to remove.  Also we might have
        // created some unreachable states, so we will simply call
        // purge_unreachable_states() later to clean this.
        if (!univ_edge_erased)
          break;
        else
          purge_unreachable_needed = true;
      }

    // Is the initial state actually useful?  If not, make this an
    // empty automaton by resetting the graph.
    bool usefulinit = true;
    for (unsigned d: univ_dests(init_number_))
      if (!useful[d])
        {
          usefulinit = false;
          break;
        }
    if (!usefulinit)
      {
        g_ = graph_t();
        init_number_ = new_state();
        prop_universal(true);
        prop_complete(false);
        prop_stutter_invariant(true);
        prop_weak(true);
        return;
      }

    // Renumber each used state.
    unsigned current = 0;
    for (unsigned s = 0; s < num_states; ++s)
      if (useful[s])
        useful[s] = current++;
      else
        useful[s] = -1U;
    if (current == num_states)
      return;                        // No useless state.

    // Removing some non-deterministic dead state could make the
    // automaton universal.  Likewise for non-complete.
    if (prop_universal().is_false())
      prop_universal(trival::maybe());
    if (prop_complete().is_false())
      prop_complete(trival::maybe());

    defrag_states(std::move(useful), current);

    if (purge_unreachable_needed)
      purge_unreachable_states();
  }

  void twa_graph::defrag_states(std::vector<unsigned>&& newst,
                                unsigned used_states)
  {
    if (!is_existential())
      {
        // Running defrag_states() on alternating automata is tricky,
        // because we want to
        // #1 rename the regular states
        // #2 rename the states in universal destinations
        // #3 get rid of the unused universal destinations
        // #4 merge identical universal destinations
        //
        // graph::degrag_states() actually does only #1. It it could
        // do #2, but that would prevent use from doing #3 and #4.  It
        // cannot do #3 and #4 because the graph object does not know
        // what an initial state is, and our initial state might be
        // universal.
        //
        // As a consequence this code preforms #2, #3, and #4 before
        // calling graph::degrag_states() to finish with #1.  We clear
        // the "dests vector" of the current automaton, recreate all
        // the new destination groups using a univ_dest_mapper to
        // simplify and unify them, and extend newst with some new
        // entries that will point the those new universal destination
        // so that graph::defrag_states() does not have to deal with
        // universal destination in any way.
        auto& g = get_graph();
        auto& dests = g.dests_vector();

        // Clear the destination vector, saving the old one.
        std::vector<unsigned> old_dests;
        std::swap(dests, old_dests);
        // dests will be updated as a side effect of declaring new
        // destination groups to uniq.
        internal::univ_dest_mapper<twa_graph::graph_t> uniq(g);

        // The newst entry associated to each of the old destination
        // group.
        std::vector<unsigned> seen(old_dests.size(), -1U);

        // Rename a state if it denotes a universal destination.  This
        // function has to be applied to the destination of each edge,
        // as well as to the initial state.  The need to work on the
        // initial state is the reason it cannot be implemented in
        // graph::defrag_states().
        auto fixup = [&](unsigned& in_dst)
          {
            unsigned dst = in_dst;
            if ((int) dst >= 0)       // not a universal edge
              return;
            dst = ~dst;
            unsigned& nd = seen[dst];
            if (nd == -1U)      // An unprocessed destination group
              {
                // store all renamed destination states in tmp
                std::vector<unsigned> tmp;
                auto begin = old_dests.data() + dst + 1;
                auto end = begin + old_dests[dst];
                while (begin != end)
                  {
                    unsigned n = newst[*begin++];
                    if (n == -1U)
                      continue;
                    tmp.emplace_back(n);
                  }
                if (tmp.empty())
                  {
                    // All destinations of this group were marked for
                    // removal.  Mark this universal transition for
                    // removal as well.  Is this really what we expect?
                    nd = -1U;
                  }
                else
                  {
                    // register this new destination group, add et two
                    // newst, and use the index in newst to relabel
                    // the state so that graph::degrag_states() will
                    // eventually update it to the correct value.
                    nd = newst.size();
                    newst.emplace_back(uniq.new_univ_dests(tmp.begin(),
                                                           tmp.end()));
                  }
              }
            in_dst = nd;
          };
        fixup(init_number_);
        for (auto& e: edges())
          fixup(e.dst);
      }

    if (auto* names = get_named_prop<std::vector<std::string>>("state-names"))
      {
        unsigned size = names->size();
        for (unsigned s = 0; s < size; ++s)
          {
            unsigned dst = newst[s];
            if (dst == s || dst == -1U)
              continue;
            (*names)[dst] = std::move((*names)[s]);
          }
        names->resize(used_states);
      }
    if (auto hs = get_named_prop<std::map<unsigned, unsigned>>
        ("highlight-states"))
      {
        std::map<unsigned, unsigned> hs2;
        for (auto p: *hs)
          {
            unsigned dst = newst[p.first];
            if (dst != -1U)
              hs2[dst] = p.second;
          }
        std::swap(*hs, hs2);
      }
    init_number_ = newst[init_number_];
    g_.defrag_states(std::move(newst), used_states);
  }

  void twa_graph::remove_unused_ap()
  {
    if (ap().empty())
      return;
    std::set<bdd> conds;
    bdd all = ap_vars();
    for (auto& e: g_.edges())
      {
        all = bdd_exist(all, bdd_support(e.cond));
        if (all == bddtrue)    // All APs are used.
          return;
      }
    auto d = get_dict();
    while (all != bddtrue)
      {
        unregister_ap(bdd_var(all));
        all = bdd_high(all);
      }
  }


}