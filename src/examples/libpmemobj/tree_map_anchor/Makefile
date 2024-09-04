#
# Copyright 2015-2017, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# examples/libpmemobj/tree_map/Makefile -- build the tree map example
#
LIBRARIES = ctree_anchor_map ctree_anchor_map_opt btree_anchor_map btree_anchor_map_opt rtree_anchor_map rtree_anchor_map_opt rbtree_anchor_map rbtree_anchor_map_opt

LIBS = -lpmemobj

include ../../Makefile.inc

libctree_anchor_map.o: ctree_anchor_map.o
libctree_anchor_map_opt.o: ctree_anchor_map_opt.o
libbtree_anchor_map.o: btree_anchor_map.o
libbtree_anchor_map_opt.o: btree_anchor_map_opt.o
librtree_anchor_map.o: rtree_anchor_map.o
librtree_anchor_map_opt.o: rtree_anchor_map_opt.o
librbtree_anchor_map.o: rbtree_anchor_map.o
librbtree_anchor_map_opt.o: rbtree_anchor_map_opt.o
