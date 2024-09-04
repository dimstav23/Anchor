#
# Copyright 2015-2018, Intel Corporation
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
# examples/libpmemobj/map_anchor/Makefile -- build the map example
#
TOP := $(dir $(lastword $(MAKEFILE_LIST)))../../../../
include $(TOP)/src/common.inc

PROGS = mapcli
LIBRARIES = map_anchor_ctree map_anchor_ctree_opt\
			map_anchor_btree map_anchor_btree_opt\
			map_anchor_rtree map_anchor_rtree_opt\
			map_anchor_rbtree map_anchor_rbtree_opt\
			map_anchor_skiplist map_anchor_skiplist_opt\
			map_hashmap_anchor_tx map_hashmap_anchor_tx_opt\
			map
#future integration
#LIBRARIES = map_anchor_ctree map_anchor_btree map_anchor_rbtree\
			map_hashmap_anchor_tx map_anchor_rtree map
		

LIBS = -lpmemobj -pthread

include ../../Makefile.inc

CFLAGS += -I../
CFLAGS += -I../hashmap_anchor
CFLAGS += -I../tree_map_anchor
CFLAGS += -I../list_map_anchor

mapcli: mapcli.o libmap.a

libmap_anchor_ctree.o: map_ctree.o map.o ../tree_map_anchor/libctree_anchor_map.a
libmap_anchor_ctree_opt.o: map_ctree_opt.o map.o ../tree_map_anchor/libctree_anchor_map_opt.a
libmap_anchor_btree.o: map_btree.o map.o ../tree_map_anchor/libbtree_anchor_map.a
libmap_anchor_btree_opt.o: map_btree_opt.o map.o ../tree_map_anchor/libbtree_anchor_map_opt.a
libmap_anchor_rtree.o: map_rtree.o map.o ../tree_map_anchor/librtree_anchor_map.a
libmap_anchor_rtree_opt.o: map_rtree_opt.o map.o ../tree_map_anchor/librtree_anchor_map_opt.a
libmap_anchor_rbtree.o: map_rbtree.o map.o ../tree_map_anchor/librbtree_anchor_map.a
libmap_anchor_rbtree_opt.o: map_rbtree_opt.o map.o ../tree_map_anchor/librbtree_anchor_map_opt.a
libmap_anchor_skiplist.o: map_skiplist.o map.o ../list_map_anchor/libskiplist_anchor_map.a
libmap_anchor_skiplist_opt.o: map_skiplist_opt.o map.o ../list_map_anchor/libskiplist_anchor_map_opt.a
libmap_hashmap_anchor_tx.o: map_hashmap_tx.o map.o ../hashmap_anchor/libhashmap_anchor_tx.a
libmap_hashmap_anchor_tx_opt.o: map_hashmap_tx_opt.o map.o ../hashmap_anchor/libhashmap_anchor_tx_opt.a

libmap.o: map.o\
	map_ctree.o map_ctree_opt.o\
	map_btree.o map_btree_opt.o\
	map_rtree.o map_rtree_opt.o\
	map_rbtree.o map_rbtree_opt.o\
	map_skiplist.o map_skiplist_opt.o\
	map_hashmap_tx.o map_hashmap_tx_opt.o\
	../tree_map_anchor/libctree_anchor_map.a\
	../tree_map_anchor/libctree_anchor_map_opt.a\
	../tree_map_anchor/libbtree_anchor_map.a\
	../tree_map_anchor/libbtree_anchor_map_opt.a\
	../tree_map_anchor/librtree_anchor_map.a\
	../tree_map_anchor/librtree_anchor_map_opt.a\
	../tree_map_anchor/librbtree_anchor_map.a\
	../tree_map_anchor/librbtree_anchor_map_opt.a\
	../list_map_anchor/libskiplist_anchor_map.a\
	../list_map_anchor/libskiplist_anchor_map_opt.a\
	../hashmap_anchor/libhashmap_anchor_tx.a\
	../hashmap_anchor/libhashmap_anchor_tx_opt.a

../tree_map_anchor/libctree_anchor_map.a:
	$(MAKE) -C ../tree_map_anchor ctree_anchor_map

../tree_map_anchor/libctree_anchor_map_opt.a:
	$(MAKE) -C ../tree_map_anchor ctree_anchor_map_opt

../tree_map_anchor/libbtree_anchor_map.a:
	$(MAKE) -C ../tree_map_anchor btree_anchor_map

../tree_map_anchor/libbtree_anchor_map_opt.a:
	$(MAKE) -C ../tree_map_anchor btree_anchor_map_opt

../tree_map_anchor/librtree_anchor_map.a:
	$(MAKE) -C ../tree_map_anchor rtree_anchor_map

../tree_map_anchor/librtree_anchor_map_opt.a:
	$(MAKE) -C ../tree_map_anchor rtree_anchor_map_opt

../tree_map_anchor/librbtree_anchor_map.a:
	$(MAKE) -C ../tree_map_anchor rbtree_anchor_map

../tree_map_anchor/librbtree_anchor_map_opt.a:
	$(MAKE) -C ../tree_map_anchor rbtree_anchor_map_opt

../list_map_anchor/libskiplist_anchor_map.a:
	$(MAKE) -C ../list_map_anchor skiplist_anchor_map

../list_map_anchor/libskiplist_anchor_map_opt.a:
	$(MAKE) -C ../list_map_anchor skiplist_anchor_map_opt

../hashmap_anchor/libhashmap_anchor_tx.a:
	$(MAKE) -C ../hashmap_anchor hashmap_anchor_tx

../hashmap_anchor/libhashmap_anchor_tx_opt.a:
	$(MAKE) -C ../hashmap_anchor hashmap_anchor_tx_opt
