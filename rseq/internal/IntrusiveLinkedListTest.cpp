/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/IntrusiveLinkedList.h"

#include <gtest/gtest.h>

using namespace rseq::internal;

struct LLInt : IntrusiveLinkedListNode<LLInt> {
  unsigned data;
};

struct DiesNoisily : IntrusiveLinkedListNode<DiesNoisily> {
  DiesNoisily() : noisy(true) {}
  ~DiesNoisily() {
    EXPECT_FALSE(noisy);
  }

  bool noisy;
};

TEST(IntrusiveLinkedList, ConstructsEmpty) {
  IntrusiveLinkedList<LLInt> list;
  int count = 0;
  for (auto& item : list) {
    ++count;
  }
  EXPECT_EQ(0, count);
}

TEST(IntrusiveLinkedList, DoesListOperations) {
  const int kNumItems = 10;
  const unsigned kItemSetMask = ((1 << kNumItems) - 1);

  LLInt itemsArr[kNumItems];
  for (int i = 0; i < kNumItems; ++i) {
    itemsArr[i].data = (1 << i);
  }

  // Add all the even indices
  IntrusiveLinkedList<LLInt> itemsList;
  for (int i = 0; i < kNumItems; ++i) {
    if (i % 2 == 0) {
      itemsList.link(&itemsArr[i]);
    }
  }

  // Make sure only the even bit positions are set.
  unsigned itemSet = 0;
  for (auto& item : itemsList) {
    itemSet |= item.data;
  }
  EXPECT_EQ(0x55555555U & kItemSetMask, itemSet);

  // Add the odds, too
  for (int i = 0; i < kNumItems; ++i) {
    if (i % 2 == 1) {
      itemsList.link(&itemsArr[i]);
    }
  }

  // Make sure *all* bits are set.
  itemSet = 0;
  for (auto& item : itemsList) {
    itemSet |= item.data;
  }
  EXPECT_EQ(kItemSetMask, itemSet);

  // Remove the items divisible by 4
  for (int i = 0; i < kNumItems; ++i) {
    if (i % 4 == 0) {
      itemsList.unlink(&itemsArr[i]);
    }
  }

  // Make sure every fourth bit is unset.
  itemSet = 0;
  for (auto& item : itemsList) {
    itemSet |= item.data;
  }
  EXPECT_EQ(0xEEEEEEEEU & kItemSetMask, itemSet);
}

TEST(IntrusiveLinkedList, DoesNotTakeOwnership) {
  DiesNoisily item;
  {
    IntrusiveLinkedList<DiesNoisily> list;
    list.link(&item);
    // Destructor runs here.
  }
  item.noisy = false;
}
