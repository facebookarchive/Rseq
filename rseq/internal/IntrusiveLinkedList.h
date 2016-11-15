/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

namespace rseq {
namespace internal {

// Intrusive linked list, using the CRTP. Does not take ownership of its
// elements.

// Supports the bare minimum interface necessary for its only use, in
// ThreadControl.

template <typename T>
class IntrusiveLinkedList;

template <typename T>
class IntrusiveLinkedListNode {
 private:
  friend class IntrusiveLinkedList<T>;

  IntrusiveLinkedListNode<T>* next;
  IntrusiveLinkedListNode<T>* prev;
};

template <typename T>
class IntrusiveLinkedList {
 public:
  IntrusiveLinkedList() {
    dummyHead_.next = &dummyTail_;
    dummyTail_.prev = &dummyHead_;
  }

  void link(IntrusiveLinkedListNode<T>* node) {
    node->next = &dummyTail_;
    node->prev = dummyTail_.prev;

    node->next->prev = node;
    node->prev->next = node;
  }

  void unlink(IntrusiveLinkedListNode<T>* node) {
    node->next->prev = node->prev;
    node->prev->next = node->next;
  }



  // We don't need real iterator support, just enough for a range-based for
  // loop.
  struct Iterator {
    IntrusiveLinkedListNode<T>* item;
    void operator++() {
      item = item->next;
    }
    T& operator*() {
      return *static_cast<T*>(item);
    }
    bool operator!=(const Iterator& other) {
      return item != other.item;
    }
  };
  Iterator begin() {
    return { dummyHead_.next };
  }
  Iterator end() {
    return { &dummyTail_ };
  }

  IntrusiveLinkedListNode<T> dummyHead_;
  IntrusiveLinkedListNode<T> dummyTail_;
};

} // namespace internal
} // namespace rseq
