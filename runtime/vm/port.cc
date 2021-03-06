// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/port.h"

#include "platform/utils.h"
#include "vm/dart_api_impl.h"
#include "vm/isolate.h"
#include "vm/message_handler.h"
#include "vm/thread.h"

namespace dart {

DECLARE_FLAG(bool, trace_isolates);

Mutex* PortMap::mutex_ = NULL;
PortMap::Entry* PortMap::map_ = NULL;
MessageHandler* PortMap::deleted_entry_ = reinterpret_cast<MessageHandler*>(1);
intptr_t PortMap::capacity_ = 0;
intptr_t PortMap::used_ = 0;
intptr_t PortMap::deleted_ = 0;
Dart_Port PortMap::next_port_ = 7111;


intptr_t PortMap::FindPort(Dart_Port port) {
  intptr_t index = port % capacity_;
  intptr_t start_index = index;
  Entry entry = map_[index];
  while (entry.handler != NULL) {
    if (entry.port == port) {
      return index;
    }
    index = (index + 1) % capacity_;
    // Prevent endless loops.
    ASSERT(index != start_index);
    entry = map_[index];
  }
  return -1;
}


void PortMap::Rehash(intptr_t new_capacity) {
  Entry* new_ports = new Entry[new_capacity];
  memset(new_ports, 0, new_capacity * sizeof(Entry));

  for (intptr_t i = 0; i < capacity_; i++) {
    Entry entry = map_[i];
    // Skip free and deleted entries.
    if (entry.port != 0) {
      intptr_t new_index = entry.port % new_capacity;
      while (new_ports[new_index].port != 0) {
        new_index = (new_index + 1) % new_capacity;
      }
      new_ports[new_index] = entry;
    }
  }
  delete[] map_;
  map_ = new_ports;
  capacity_ = new_capacity;
  deleted_ = 0;
}


Dart_Port PortMap::AllocatePort() {
  Dart_Port result = next_port_;

  do {
    // TODO(iposva): Use an approved hashing function to have less predictable
    // port ids, or make them not accessible from Dart code or both.
    next_port_++;
  } while (FindPort(next_port_) >= 0);

  ASSERT(result != 0);
  return result;
}


void PortMap::SetLive(Dart_Port port) {
  MutexLocker ml(mutex_);
  intptr_t index = FindPort(port);
  ASSERT(index >= 0);
  map_[index].live = true;
  map_[index].handler->increment_live_ports();
}


void PortMap::MaintainInvariants() {
  intptr_t empty = capacity_ - used_ - deleted_;
  if (used_ > ((capacity_ / 4) * 3)) {
    // Grow the port map.
    Rehash(capacity_ * 2);
  } else if (empty < deleted_) {
    // Rehash without growing the table to flush the deleted slots out of the
    // map.
    Rehash(capacity_);
  }
}


Dart_Port PortMap::CreatePort(MessageHandler* handler) {
  ASSERT(handler != NULL);
  MutexLocker ml(mutex_);
#if defined(DEBUG)
  handler->CheckAccess();
#endif

  Entry entry;
  entry.port = AllocatePort();
  entry.handler = handler;
  entry.live = false;

  // Search for the first unused slot. Make use of the knowledge that here is
  // currently no port with this id in the port map.
  ASSERT(FindPort(entry.port) < 0);
  intptr_t index = entry.port % capacity_;
  Entry cur = map_[index];
  // Stop the search at the first found unused (free or deleted) slot.
  while (cur.port != 0) {
    index = (index + 1) % capacity_;
    cur = map_[index];
  }

  // Insert the newly created port at the index.
  ASSERT(index >= 0);
  ASSERT(index < capacity_);
  ASSERT(map_[index].port == 0);
  ASSERT((map_[index].handler == NULL) ||
         (map_[index].handler == deleted_entry_));
  if (map_[index].handler == deleted_entry_) {
    // Consuming a deleted entry.
    deleted_--;
  }
  map_[index] = entry;

  // Increment number of used slots and grow if necessary.
  used_++;
  MaintainInvariants();

  return entry.port;
}


bool PortMap::ClosePort(Dart_Port port) {
  MessageHandler* handler = NULL;
  {
    MutexLocker ml(mutex_);
    intptr_t index = FindPort(port);
    if (index < 0) {
      return false;
    }
    ASSERT(index < capacity_);
    ASSERT(map_[index].port != 0);
    ASSERT(map_[index].handler != deleted_entry_);
    ASSERT(map_[index].handler != NULL);

    handler = map_[index].handler;
#if defined(DEBUG)
    handler->CheckAccess();
#endif
    // Before releasing the lock mark the slot in the map as deleted. This makes
    // it possible to release the port map lock before flushing all of its
    // pending messages below.
    map_[index].port = 0;
    map_[index].handler = deleted_entry_;
    if (map_[index].live) {
      handler->decrement_live_ports();
    }

    used_--;
    deleted_++;
    MaintainInvariants();
  }
  handler->ClosePort(port);
  if (!handler->HasLivePorts() && handler->OwnedByPortMap()) {
    delete handler;
  }
  return true;
}


void PortMap::ClosePorts(MessageHandler* handler) {
  {
    MutexLocker ml(mutex_);
    for (intptr_t i = 0; i < capacity_; i++) {
      if (map_[i].handler == handler) {
        // Mark the slot as deleted.
        map_[i].port = 0;
        map_[i].handler = deleted_entry_;
        if (map_[i].live) {
          handler->decrement_live_ports();
        }
        used_--;
        deleted_++;
      }
    }
    MaintainInvariants();
  }
  handler->CloseAllPorts();
}


bool PortMap::PostMessage(Message* message) {
  MutexLocker ml(mutex_);
  intptr_t index = FindPort(message->dest_port());
  if (index < 0) {
    delete message;
    return false;
  }
  ASSERT(index >= 0);
  ASSERT(index < capacity_);
  MessageHandler* handler = map_[index].handler;
  ASSERT(map_[index].port != 0);
  ASSERT((handler != NULL) && (handler != deleted_entry_));
  handler->PostMessage(message);
  return true;
}


void PortMap::InitOnce() {
  mutex_ = new Mutex();

  static const intptr_t kInitialCapacity = 8;
  // TODO(iposva): Verify whether we want to keep exponentially growing.
  ASSERT(Utils::IsPowerOfTwo(kInitialCapacity));
  map_ = new Entry[kInitialCapacity];
  memset(map_, 0, kInitialCapacity * sizeof(Entry));
  capacity_ = kInitialCapacity;
  used_ = 0;
  deleted_ = 0;
}

}  // namespace dart
