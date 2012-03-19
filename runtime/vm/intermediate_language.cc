// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/intermediate_language.h"

#include "vm/object.h"
#include "vm/os.h"
#include "vm/scopes.h"

namespace dart {

// ==== Support for visiting flow graphs.
#define DEFINE_ACCEPT(ShortName, ClassName)                                    \
  void ClassName::Accept(FlowGraphVisitor* visitor) {                          \
    visitor->Visit##ShortName(this);                                           \
  }

FOR_EACH_COMPUTATION(DEFINE_ACCEPT)

#undef DEFINE_ACCEPT


Instruction* JoinEntryInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitJoinEntry(this);
  return successor_;
}


Instruction* TargetEntryInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitTargetEntry(this);
  return successor_;
}


Instruction* PickTempInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitPickTemp(this);
  return successor_;
}


Instruction* TuckTempInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitTuckTemp(this);
  return successor_;
}


Instruction* DoInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitDo(this);
  return successor_;
}


Instruction* BindInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitBind(this);
  return successor_;
}


Instruction* ReturnInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitReturn(this);
  return NULL;
}


Instruction* ThrowInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitThrow(this);
  return NULL;
}


Instruction* ReThrowInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitReThrow(this);
  return NULL;
}


Instruction* BranchInstr::Accept(FlowGraphVisitor* visitor) {
  visitor->VisitBranch(this);
  return NULL;
}


// Default implementation of visiting basic blocks.  Can be overridden.
void FlowGraphVisitor::VisitBlocks() {
  for (intptr_t i = 0; i < block_order_.length(); ++i) {
    Instruction* current = block_order_[i]->Accept(this);
    while ((current != NULL) && !current->IsBlockEntry()) {
      current = current->Accept(this);
    }
  }
}


// ==== Postorder graph traversal.
void JoinEntryInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  // The global graph entry is a TargetEntryInstr, so we can assume
  // current_block is non-null and preorder array is non-empty.
  ASSERT(current_block != NULL);
  ASSERT(!preorder->is_empty());

  // 1. Record control-flow-graph basic-block predecessors.
  predecessors_.Add(current_block);

  // 2. If the block has already been reached by the traversal, we are done.
  if (preorder_number() >= 0) return;

  // 3. The last entry in the preorder array is the spanning-tree parent.
  parent->Add(preorder->Last());

  // 4. Assign preorder number and add the block entry to the list.
  set_preorder_number(preorder->length());
  preorder->Add(this);
  // The preorder and parent arrays are both indexed by preorder block
  // number, so they should stay in lockstep.
  ASSERT(preorder->length() == parent->length());

  // 5. Recursively visit the successor.
  ASSERT(successor_ != NULL);
  successor_->DiscoverBlocks(this, preorder, postorder, parent);

  // 6. Assign postorder number and add the block entry to the list.
  set_postorder_number(postorder->length());
  postorder->Add(this);
}


void TargetEntryInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  // 1. Record control-flow-graph basic-block predecessors.
  ASSERT(predecessor_ == NULL);
  predecessor_ = current_block;  // Might be NULL (for the graph entry).

  // 2. There is a single predecessor, so we should only reach this block once.
  ASSERT(preorder_number() == -1);

  // 3. The last entry in the preorder array is the spanning-tree parent.
  // The global graph entry has a NULL parent.
  parent->Add(preorder->is_empty() ? NULL : preorder->Last());

  // 4. Assign preorder number and add the block entry to the list.
  set_preorder_number(preorder->length());
  preorder->Add(this);
  // The preorder and parent arrays are indexed by preorder block number, so
  // they should stay in lockstep.
  ASSERT(preorder->length() == parent->length());

  // 5. Recursively visit the successor.
  ASSERT(successor_ != NULL);
  successor_->DiscoverBlocks(this, preorder, postorder, parent);

  // 6. Assign postorder number and add the block entry to the list.
  set_postorder_number(postorder->length());
  postorder->Add(this);
}


void PickTempInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
  ASSERT(successor_ != NULL);
  successor_->DiscoverBlocks(current_block, preorder, postorder, parent);
}


void TuckTempInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
  ASSERT(successor_ != NULL);
  successor_->DiscoverBlocks(current_block, preorder, postorder, parent);
}


void DoInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
  ASSERT(successor_ != NULL);
  successor_->DiscoverBlocks(current_block, preorder, postorder, parent);
}


void BindInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
  ASSERT(successor_ != NULL);
  successor_->DiscoverBlocks(current_block, preorder, postorder, parent);
}


void ReturnInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
}


void ThrowInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
}


void ReThrowInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
}


void BranchInstr::DiscoverBlocks(
    BlockEntryInstr* current_block,
    GrowableArray<BlockEntryInstr*>* preorder,
    GrowableArray<BlockEntryInstr*>* postorder,
    GrowableArray<BlockEntryInstr*>* parent) {
  current_block->set_last_instruction(this);
  // Visit the false successor before the true successor so they appear in
  // true/false order in reverse postorder used as the block ordering in the
  // nonoptimizing compiler.
  ASSERT(true_successor_ != NULL);
  ASSERT(false_successor_ != NULL);
  false_successor_->DiscoverBlocks(current_block, preorder, postorder, parent);
  true_successor_->DiscoverBlocks(current_block, preorder, postorder, parent);
}


}  // namespace dart
