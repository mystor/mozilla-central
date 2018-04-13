#!/usr/bin/env python
# perfecthash.py - Helper for generating perfect hash functions for xptcodegen.py
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

FNV_OFFSET_BASIS = 0x811C9DC5
FNV_PRIME = 16777619

# We use uint32_ts for our arrays in PerfectHash. 0x80000000 is the high bit
# which we sometimes use as a flag.
U32_HIGH_BIT = 0x80000000

# A basic FNV-based hash function. bytes is the bytearray to hash. 32-bit FNV is
# used for indexing into the first table, and the value stored in that table is
# used as the offset basis for indexing into the values table.
#
# NOTE: C++ implementation is in xptinfo.cpp
def hash(bytes, h=FNV_OFFSET_BASIS):
    for byte in bytes:
        h ^= byte       # xor-in the byte
        h *= FNV_PRIME  # Multiply by the FNV prime
        h &= 0xffffffff # clamp to 32-bits
    return h

class PerfectHash(object):
    """An object representing a perfect hash function"""
    def __init__(self, intsize, data):
        """Keys should be a list of (bytearray, value) pairs"""
        mapsize = len(data) # Size of the target values array

        buckets = [(i, []) for i in range(intsize)]
        self.inter = [0] * intsize
        self.values = [None] * mapsize

        assert mapsize < U32_HIGH_BIT, \
            "Not enough space in uint32_t to index %d values" % mapsize

        # Determine which input strings map to which buckets in the intermediate array.
        for key, val in data:
            assert isinstance(key, bytearray), \
                "data should be a list of (bytearray, value) pairs"
            buckets[hash(key) % intsize][1].append((key, val))
        # Look at the largest bucket first.
        buckets.sort(key=lambda b: len(b[1]), reverse=True)

        freecursor = 0
        for idx, bucket in buckets:
            # If we've reached buckets with no conflicts, we can just start
            # storing direct indices into the final array.
            # The high bit is set to identify direct indices.
            if len(bucket) == 0:
                break
            elif len(bucket) == 1:
                while freecursor < mapsize:
                    if self.values[freecursor] is None:
                        self.inter[idx] = freecursor | U32_HIGH_BIT
                        self.values[freecursor] = bucket[0][1]
                        break
                    freecursor += 1
                continue

            # Try values for the basis until we find one with no conflicts.
            i = 0
            basis = 1
            slots = []
            while i < len(bucket):
                slot = hash(bucket[i][0], basis) % mapsize
                if self.values[slot] is not None or slot in slots:
                    # There was a conflict, try the next basis.
                    basis += 1
                    i = 0
                    del slots[:]
                else:
                    slots.append(slot)
                    i += 1

            assert basis < U32_HIGH_BIT, \
                "not enough space in uint32_t to store bases %d" % basis

            # We've found a basis which doesn't conflict
            self.inter[idx] = basis
            for slot, (key, val) in zip(slots, bucket):
                self.values[slot] = val

    def lookup(self, key):
        mid = self.inter[hash(key) % len(self.inter)]
        if mid & U32_HIGH_BIT:
            return self.values[mid & ~U32_HIGH_BIT]
        else:
            return self.values[hash(key, mid) % len(self.values)]
