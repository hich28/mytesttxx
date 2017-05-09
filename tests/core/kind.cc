// -*- coding: utf-8 -*-
// Copyright (C) 2010, 2012, 2015, 2016 Laboratoire de Recherche et
// Developement de l'Epita (LRDE).
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

#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <cstdlib>
#include <spot/tl/parse.hh>

static void
syntax(char *prog)
{
  std::cerr << prog << " formula" << std::endl;
  exit(2);
}

int
main(int argc, char **argv)
{
  if (argc != 2)
    syntax(argv[0]);

  std::ifstream input(argv[1]);
  if (!input)
    {
      std::cerr << "failed to open " << argv[1] << '\n';
      return 2;
    }

  std::string s;
  while (std::getline(input, s))
    {
      if (s[0] == '#')                // Skip comments
        {
          std::cerr << s << '\n';
          continue;
        }
      std::istringstream ss(s);
      std::string form;
      std::string expected;
      std::getline(ss, form, ',');
      std::getline(ss, expected);

      spot::parse_error_list p1;
      auto pf1 = spot::parse_infix_psl(form);
      if (pf1.format_errors(std::cerr))
        return 2;
      auto f1 = pf1.f;

      std::ostringstream so;
      spot::print_formula_props(so, f1, true);
      auto sost = so.str();
      std::cout << form << ',' << sost << '\n';
      if (sost != expected)
        {
          std::cerr << "computed '" << sost
                    << "' but expected '" << expected << "'\n";
          return 2;
        }
    }
  assert(spot::fnode::instances_check());
  return 0;
}
