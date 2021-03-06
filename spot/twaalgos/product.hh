// -*- coding: utf-8 -*-
// Copyright (C) 2014, 2015 Laboratoire de Recherche et
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

#pragma once

#include <spot/misc/common.hh>
#include <spot/twa/fwd.hh>
#include <vector>
#include <utility>

namespace spot
{
  // The tgba constructed by product() below contain property named
  // "product-states" with type product_states.
  typedef std::vector<std::pair<unsigned, unsigned>> product_states;

  SPOT_API
  twa_graph_ptr product(const const_twa_graph_ptr& left,
                        const const_twa_graph_ptr& right);

  SPOT_API
  twa_graph_ptr product(const const_twa_graph_ptr& left,
                        const const_twa_graph_ptr& right,
                        unsigned left_state,
                        unsigned right_state);

  SPOT_API
  twa_graph_ptr product_or(const const_twa_graph_ptr& left,
                           const const_twa_graph_ptr& right);

  SPOT_API
  twa_graph_ptr product_or(const const_twa_graph_ptr& left,
                           const const_twa_graph_ptr& right,
                           unsigned left_state,
                           unsigned right_state);

}
