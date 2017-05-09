# -*- coding: utf-8 -*-
# Copyright (C) 2016, 2017 Laboratoire de Recherche et Développement
# de l'Epita (LRDE)
#
# This file is part of Spot, a model checking library.
#
# Spot is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Spot is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
# License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http:#www.gnu.org/licenses/>.

import spot
import sys


def hstates(txt):
    for line in txt.split('\n'):
        if line.startswith('spot.highlight.states:'):
            return line[23:]
    return ''


def test(f, opt, expected):
    aut = spot.translate(f, opt, 'deterministic')
    v = spot.language_map(aut)
    assert len(v) == aut.num_states()
    spot.highlight_languages(aut)
    l = hstates(aut.to_str('hoa', '1.1'))
    if l != expected:
        print('for {}\nexpected: {}\n but got: {}'.format(f, expected, l),
              file=sys.stderr)
        exit(1)


test('GF(a) & GFb & c', 'BA', '1 0 2 0 3 0')
test('GF(a) & c & X!a', 'BA', '2 0 3 0')
test('(a U b) & GF(c & Xd)', 'generic', '1 0 2 0')
test('GF(a <-> Xb) & Fb', 'generic', '0 0 1 1 2 0 3 1 4 1')
test('Xa', 'BA', '')

# Non-deterministic automata are not supported
try:
    test('FGa', 'BA', '')
except RuntimeError as e:
    assert 'language_map only works with deterministic automata'in str(e)
else:
    exit(1)
