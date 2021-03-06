// -*- coding: utf-8 -*-
// Copyright (C) 2013, 2015-2017 Laboratoire de Recherche et
// Developpement de l'Epita (LRDE).
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

#include <spot/twaalgos/isunamb.hh>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <spot/twaalgos/mask.hh>
#include <set>
#include <list>

namespace spot
{
  // Conceptually, aut is unambiguous if the useful part of aut has
  // the same size as the useful part of aut*aut.
  //
  // However calling scc_info::determine_unknown_acceptance(), which
  // is needed to decide which states are actually useless, is costly.
  // We do it on aut, but we avoid doing it on prod.
  //
  // This optimization, which requires much more code than what
  // we used to have, was motivated by issue #188.
  bool is_unambiguous(const const_twa_graph_ptr& aut)
  {
    if (!aut->is_existential())
      throw std::runtime_error
        ("is_unambiguous() does not support alternation");

    trival u = aut->prop_unambiguous();
    if (u.is_known())
      return u.is_true();
    if (aut->num_edges() == 0)
      return true;

    scc_info sccmap(aut);
    sccmap.determine_unknown_acceptance();
    unsigned autsz = aut->num_states();
    std::vector<bool> v;
    v.reserve(autsz);
    bool all_useful = true;
    for (unsigned n = 0; n < autsz; ++n)
      {
        bool useful = sccmap.is_useful_state(n);
        all_useful &= useful;
        v.push_back(useful);
      }

    // If the input automaton comes from any /decent/ source, it is
    // unlikely that it has some useless states, so do not bother too
    // much optimizing this case.
    if (!all_useful)
      return is_unambiguous(mask_keep_accessible_states
                            (aut, v, aut->get_init_state_number()));

    // Reuse v to remember which states are in an accepting SCC.
    for (unsigned n = 0; n < autsz; ++n)
      v[n] = sccmap.is_accepting_scc(sccmap.scc_of(n));

    auto prod = product(aut, aut);
    auto sprod =
      prod->get_named_prop<std::vector<std::pair<unsigned,
                                                 unsigned>>>("product-states");
    assert(sprod);

    // What follow is a way to compute whether an SCC is useless in
    // prod, without having to call
    // scc_map::determine_unknown_acceptance() on scc_map(prod),
    // because prod has potentially a large acceptance condition.
    //
    // We know that an SCC of the product is accepting iff it is the
    // combination of two accepting SCCs of the original automaton.
    //
    // So we can just compute the acceptance of each SCC this way, and
    // derive the usefulness from that.
    scc_info sccmap_prod(prod);
    unsigned psc = sccmap_prod.scc_count();
    std::vector<bool> useful;
    useful.reserve(psc);
    for (unsigned n = 0; n < psc; ++n)
      {
        unsigned one_state = sccmap_prod.states_of(n).front();
        bool accepting =
          v[(*sprod)[one_state].first] && v[(*sprod)[one_state].second];
        if (accepting)
          {
            useful[n] = true;
            continue;
          }
        bool uf = false;
        for (unsigned j: sccmap_prod.succ(n))
          if (useful[j])
            {
              uf = true;
              break;
            }
        useful[n] = uf;
      }

    // Now we just have to count the number of states && edges that
    // belong to the useful part of the automaton.
    unsigned np = prod->num_states();
    v.resize(np);
    unsigned useful_states = 0;
    for (unsigned n = 0; n < np; ++n)
      {
        bool uf = useful[sccmap_prod.scc_of(n)];
        v[n] = uf;
        useful_states += uf;
      }

    if (aut->num_states() != useful_states)
      return false;

    unsigned useful_edges = 0;
    for (const auto& e: prod->edges())
      useful_edges += v[e.src] && v[e.dst];

    return aut->num_edges() == useful_edges;
  }

  bool check_unambiguous(const twa_graph_ptr& aut)
  {
    bool u = is_unambiguous(aut);
    aut->prop_unambiguous(u);
    return u;
  }
}
