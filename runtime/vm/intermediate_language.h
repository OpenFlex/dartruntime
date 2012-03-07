// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_INTERMEDIATE_LANGUAGE_H_
#define VM_INTERMEDIATE_LANGUAGE_H_

#include "vm/allocation.h"
#include "vm/ast.h"
#include "vm/growable_array.h"
#include "vm/handles_impl.h"
#include "vm/object.h"

namespace dart {

class FlowGraphVisitor;
class LocalVariable;

// Computations and values.
//
// <Computation> ::=
//   <Value>
// | AssertAssignable <Value> <AbstractType>
// | InstanceCall <AstNode> <String> <Value> ...
// | StaticCall <StaticCallNode> <Value> ...
// | LoadLocal <LocalVariable>
// | StoreLocal <LocalVariable> <Value>
// | StrictCompare <Token::kind> <Value> <Value>
// | NativeCall <NativeBodyNode>
// | StoreIndexed <StoreIndexedNode> <Value> <Value> <Value>
// | InstanceSetter <InstanceSetterNode> <Value> <Value>
// | LoadInstanceField <LoadInstanceFieldNode> <Value>
// | StoreInstanceField <StoreInstanceFieldNode> <Value> <Value>
// | LoadStaticField <Field>
// | StoreStaticField <StoreStaticFieldNode> <Value>
// | BooleanNegate <Value>
// | InstanceOf <Value> <Type>
//
// <Value> ::=
//   Temp <int>
// | Constant <Instance>

// M is a two argument macro.  It is applied to each concrete value's
// typename and classname.
#define FOR_EACH_VALUE(M)                                                      \
  M(Temp, TempVal)                                                             \
  M(Constant, ConstantVal)                                                     \


// M is a two argument macro.  It is applied to each concrete instruction's
// (including the values) typename and classname.
#define FOR_EACH_COMPUTATION(M)                                                \
  FOR_EACH_VALUE(M)                                                            \
  M(AssertAssignable, AssertAssignableComp)                                    \
  M(InstanceCall, InstanceCallComp)                                            \
  M(StaticCall, StaticCallComp)                                                \
  M(LoadLocal, LoadLocalComp)                                                  \
  M(StoreLocal, StoreLocalComp)                                                \
  M(StrictCompare, StrictCompareComp)                                          \
  M(NativeCall, NativeCallComp)                                                \
  M(StoreIndexed, StoreIndexedComp)                                            \
  M(InstanceSetter, InstanceSetterComp)                                        \
  M(LoadInstanceField, LoadInstanceFieldComp)                                  \
  M(StoreInstanceField, StoreInstanceFieldComp)                                \
  M(LoadStaticField, LoadStaticFieldComp)                                      \
  M(StoreStaticField, StoreStaticFieldComp)                                    \
  M(BooleanNegate, BooleanNegateComp)                                          \
  M(InstanceOf, InstanceOfComp)


#define FORWARD_DECLARATION(ShortName, ClassName) class ClassName;
FOR_EACH_COMPUTATION(FORWARD_DECLARATION)
#undef FORWARD_DECLARATION

class Computation : public ZoneAllocated {
 public:
  Computation() { }

  // Visiting support.
  virtual void Accept(FlowGraphVisitor* visitor) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Computation);
};


class Value : public Computation {
 public:
  Value() { }

#define DEFINE_TESTERS(ShortName, ClassName)                                   \
  virtual ClassName* As##ShortName() { return NULL; }                          \
  bool Is##ShortName() { return As##ShortName() != NULL; }

  FOR_EACH_VALUE(DEFINE_TESTERS)
#undef DEFINE_TESTERS

 private:
  DISALLOW_COPY_AND_ASSIGN(Value);
};


// Functions defined in all concrete computation classes.
#define DECLARE_COMPUTATION(ShortName)                                         \
  virtual void Accept(FlowGraphVisitor* visitor);

// Functions defined in all concrete value classes.
#define DECLARE_VALUE(ShortName)                                               \
  DECLARE_COMPUTATION(ShortName)                                               \
  virtual ShortName##Val* As##ShortName() { return this; }


class TempVal : public Value {
 public:
  explicit TempVal(intptr_t index) : index_(index) { }

  DECLARE_VALUE(Temp)

  intptr_t index() const { return index_; }

 private:
  const intptr_t index_;

  DISALLOW_COPY_AND_ASSIGN(TempVal);
};


class ConstantVal: public Value {
 public:
  explicit ConstantVal(const Instance& instance) : instance_(instance) {
    ASSERT(instance.IsZoneHandle());
  }

  DECLARE_VALUE(Constant)

  const Instance& instance() const { return instance_; }

 private:
  const Instance& instance_;

  DISALLOW_COPY_AND_ASSIGN(ConstantVal);
};

#undef DECLARE_VALUE


class AssertAssignableComp : public Computation {
 public:
  AssertAssignableComp(Value* value, const AbstractType& type)
      : value_(value), type_(type) { }

  DECLARE_COMPUTATION(AssertAssignable)

  Value* value() const { return value_; }
  const AbstractType& type() const { return type_; }

 private:
  Value* value_;
  const AbstractType& type_;

  DISALLOW_COPY_AND_ASSIGN(AssertAssignableComp);
};


class InstanceCallComp : public Computation {
 public:
  InstanceCallComp(intptr_t node_id,
                   intptr_t token_index,
                   const String& function_name,
                   ZoneGrowableArray<Value*>* arguments,
                   const Array& argument_names,
                   intptr_t checked_argument_count)
      : node_id_(node_id),
        token_index_(token_index),
        function_name_(function_name),
        arguments_(arguments),
        argument_names_(argument_names),
        checked_argument_count_(checked_argument_count) {
    ASSERT(function_name.IsZoneHandle());
    ASSERT(!arguments->is_empty());
    ASSERT(argument_names.IsZoneHandle());
  }

  DECLARE_COMPUTATION(InstanceCall)

  intptr_t node_id() const { return node_id_; }
  intptr_t token_index() const { return token_index_; }
  const String& function_name() const { return function_name_; }
  int ArgumentCount() const { return arguments_->length(); }
  Value* ArgumentAt(int index) const { return (*arguments_)[index]; }
  const Array& argument_names() const { return argument_names_; }
  intptr_t checked_argument_count() const { return checked_argument_count_; }

 private:
  const intptr_t node_id_;
  const intptr_t token_index_;
  const String& function_name_;
  ZoneGrowableArray<Value*>* const arguments_;
  const Array& argument_names_;
  const intptr_t checked_argument_count_;

  DISALLOW_COPY_AND_ASSIGN(InstanceCallComp);
};


class StrictCompareComp : public Computation {
 public:
  StrictCompareComp(Token::Kind kind, Value* left, Value* right)
      : kind_(kind), left_(left), right_(right) {
    ASSERT((kind_ == Token::kEQ_STRICT) || (kind_ == Token::kNE_STRICT));
  }

  DECLARE_COMPUTATION(StrictCompare)

  Token::Kind kind() const { return kind_; }
  Value* left() const { return left_; }
  Value* right() const { return right_; }

 private:
  const Token::Kind kind_;
  Value* left_;
  Value* right_;

  DISALLOW_COPY_AND_ASSIGN(StrictCompareComp);
};


class StaticCallComp : public Computation {
 public:
  StaticCallComp(StaticCallNode* node, ZoneGrowableArray<Value*>* arguments)
      : ast_node_(*node), arguments_(arguments) { }

  DECLARE_COMPUTATION(StaticCall)

  // Accessors forwarded to the AST node.
  const Function& function() const { return ast_node_.function(); }
  const Array& argument_names() const { return ast_node_.arguments()->names(); }
  intptr_t token_index() const { return ast_node_.token_index(); }

  int ArgumentCount() const { return arguments_->length(); }
  Value* ArgumentAt(int index) const { return (*arguments_)[index]; }

 private:
  const StaticCallNode& ast_node_;
  ZoneGrowableArray<Value*>* arguments_;

  DISALLOW_COPY_AND_ASSIGN(StaticCallComp);
};


class LoadLocalComp : public Computation {
 public:
  explicit LoadLocalComp(const LocalVariable& local) : local_(local) { }

  DECLARE_COMPUTATION(LoadLocal)

  const LocalVariable& local() const { return local_; }

 private:
  const LocalVariable& local_;

  DISALLOW_COPY_AND_ASSIGN(LoadLocalComp);
};


class StoreLocalComp : public Computation {
 public:
  StoreLocalComp(const LocalVariable& local, Value* value)
      : local_(local), value_(value) { }

  DECLARE_COMPUTATION(StoreLocal)

  const LocalVariable& local() const { return local_; }
  Value* value() const { return value_; }

 private:
  const LocalVariable& local_;
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(StoreLocalComp);
};


class NativeCallComp : public Computation {
 public:
  explicit NativeCallComp(NativeBodyNode* node) : ast_node_(*node) {}

  DECLARE_COMPUTATION(NativeCall)

  const String& native_name() const {
    return ast_node_.native_c_function_name();
  }

 private:
  const NativeBodyNode& ast_node_;

  DISALLOW_COPY_AND_ASSIGN(NativeCallComp);
};


class LoadInstanceFieldComp : public Computation {
 public:
  LoadInstanceFieldComp(LoadInstanceFieldNode* ast_node, Value* instance)
      : ast_node_(*ast_node), instance_(instance) {
    ASSERT(instance_ != NULL);
  }

  DECLARE_COMPUTATION(LoadInstanceFieldComp)

  const Field& field() const { return ast_node_.field(); }

  Value* instance() const { return instance_; }

 private:
  const LoadInstanceFieldNode& ast_node_;
  Value* instance_;

  DISALLOW_COPY_AND_ASSIGN(LoadInstanceFieldComp);
};


class StoreInstanceFieldComp : public Computation {
 public:
  StoreInstanceFieldComp(StoreInstanceFieldNode* ast_node,
                         Value* instance,
                         Value* value)
      : ast_node_(*ast_node), instance_(instance), value_(value) {
    ASSERT(instance_ != NULL);
    ASSERT(value_ != NULL);
  }

  DECLARE_COMPUTATION(StoreInstanceFieldComp)

  intptr_t node_id() const { return ast_node_.id(); }
  intptr_t token_index() const { return ast_node_.token_index(); }
  const Field& field() const { return ast_node_.field(); }

  Value* instance() const { return instance_; }
  Value* value() const { return value_; }

 private:
  const StoreInstanceFieldNode& ast_node_;
  Value* instance_;
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(StoreInstanceFieldComp);
};


class LoadStaticFieldComp : public Computation {
 public:
  explicit LoadStaticFieldComp(const Field& field) : field_(field) {}

  DECLARE_COMPUTATION(LoadStaticFieldComp);

  const Field& field() const { return field_; }

 private:
  const Field& field_;

  DISALLOW_COPY_AND_ASSIGN(LoadStaticFieldComp);
};


class StoreStaticFieldComp : public Computation {
 public:
  StoreStaticFieldComp(StoreStaticFieldNode* ast_node, Value* value)
      : ast_node_(*ast_node), value_(value) {
    ASSERT(value != NULL);
  }

  DECLARE_COMPUTATION(StoreStaticFieldComp);

  intptr_t token_index() const { return ast_node_.token_index(); }
  intptr_t node_id() const { return ast_node_.id(); }
  const Field& field() const { return ast_node_.field(); }
  Value* value() const { return value_; }

 private:
  const StoreStaticFieldNode& ast_node_;
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(StoreStaticFieldComp);
};


// Not simply an InstanceCall because it has somewhat more complicated
// semantics: the value operand is preserved before the call.
class StoreIndexedComp : public Computation {
 public:
  StoreIndexedComp(StoreIndexedNode* node,
                   Value* array,
                   Value* index,
                   Value* value)
      : ast_node_(*node),
        array_(array),
        index_(index),
        value_(value) { }

  DECLARE_COMPUTATION(StoreIndexed)

  // Accessors forwarded to the AST node.
  intptr_t node_id() const { return ast_node_.id(); }
  intptr_t token_index() const { return ast_node_.token_index(); }

  Value* array() const { return array_; }
  Value* index() const { return index_; }
  Value* value() const { return value_; }

 private:
  const StoreIndexedNode& ast_node_;
  Value* array_;
  Value* index_;
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(StoreIndexedComp);
};


// Not simply an InstanceCall because it has somewhat more complicated
// semantics: the value operand is preserved before the call.
class InstanceSetterComp : public Computation {
 public:
  InstanceSetterComp(InstanceSetterNode* node,
                     Value* receiver,
                     Value* value)
      : ast_node_(*node),
        receiver_(receiver),
        value_(value) { }

  DECLARE_COMPUTATION(InstanceSetter)

  // Accessors forwarded to the AST node.
  intptr_t node_id() const { return ast_node_.id(); }
  intptr_t token_index() const { return ast_node_.token_index(); }
  const String& field_name() const { return ast_node_.field_name(); }

  Value* receiver() const { return receiver_; }
  Value* value() const { return value_; }

 private:
  const InstanceSetterNode& ast_node_;
  Value* receiver_;
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(InstanceSetterComp);
};


// Note overrideable, built-in: value? false : true.
class BooleanNegateComp : public Computation {
 public:
  explicit BooleanNegateComp(Value* value) : value_(value) {}

  DECLARE_COMPUTATION(BooleanNegateComp)

  Value* value() const { return value_; }

 private:
  Value* value_;

  DISALLOW_COPY_AND_ASSIGN(BooleanNegateComp);
};


class InstanceOfComp : public Computation {
 public:
  InstanceOfComp(intptr_t node_id,
                 intptr_t token_index,
                 Value* value,
                 const AbstractType& type,
                 bool negate_result)
      : node_id_(node_id),
        token_index_(token_index),
        value_(value),
        type_(type),
        negate_result_(negate_result) {}

  DECLARE_COMPUTATION(InstanceOfComp)

  Value* value() const { return value_; }
  bool negate_result() const { return negate_result_; }
  const AbstractType& type() const { return type_; }

 private:
  const intptr_t node_id_;
  const intptr_t token_index_;
  Value* value_;
  const AbstractType& type_;
  const bool negate_result_;

  DISALLOW_COPY_AND_ASSIGN(InstanceOfComp);
};


#undef DECLARE_COMPUTATION


// Instructions.
//
// <Instruction> ::= JoinEntry <Instruction>
//                 | TargetEntry <Instruction>
//                 | PickTemp <int> <int> <Instruction>
//                 | TuckTemp <int> <int> <Instruction>
//                 | Do <Computation> <Instruction>
//                 | Bind <int> <Computation> <Instruction>
//                 | Return <Value>
//                 | Branch <Value> <Instruction> <Instruction>

// M is a single argument macro.  It is applied to each concrete instruction
// type name.  The concrete instruction classes are the name with Instr
// concatenated.
#define FOR_EACH_INSTRUCTION(M)                                                \
  M(JoinEntry)                                                                 \
  M(TargetEntry)                                                               \
  M(PickTemp)                                                                  \
  M(TuckTemp)                                                                  \
  M(Do)                                                                        \
  M(Bind)                                                                      \
  M(Return)                                                                    \
  M(Branch)                                                                    \


// Forward declarations for Instruction classes.
class BlockEntryInstr;
#define FORWARD_DECLARATION(type) class type##Instr;
FOR_EACH_INSTRUCTION(FORWARD_DECLARATION)
#undef FORWARD_DECLARATION


// Functions required in all concrete instruction classes.
#define DECLARE_INSTRUCTION(type)                                              \
  virtual Instruction* Accept(FlowGraphVisitor* visitor);                      \
  virtual bool Is##type() const { return true; }                               \
  virtual type##Instr* As##type() { return this; }                             \


class Instruction : public ZoneAllocated {
 public:
  Instruction() : mark_(false) { }

  virtual bool IsBlockEntry() const { return false; }

  // Visiting support.
  virtual Instruction* Accept(FlowGraphVisitor* visitor) = 0;

  virtual void SetSuccessor(Instruction* instr) = 0;
  // Perform a postorder traversal of the instruction graph reachable from
  // this instruction.  Accumulate basic block entries in the order visited
  // in the in/out parameter 'block_entries'.
  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries) = 0;

  // Mark bit to support non-reentrant recursive traversal (i.e.,
  // identification of cycles).  Before and after a traversal, all the nodes
  // must have the same mark.
  bool mark() const { return mark_; }
  void flip_mark() { mark_ = !mark_; }

#define INSTRUCTION_TYPE_CHECK(type)                                           \
  virtual bool Is##type() const { return false; }                              \
  virtual type##Instr* As##type() { return NULL; }
FOR_EACH_INSTRUCTION(INSTRUCTION_TYPE_CHECK)
#undef INSTRUCTION_TYPE_CHECK

 private:
  bool mark_;

  DISALLOW_COPY_AND_ASSIGN(Instruction);
};


// Basic block entries are administrative nodes.  Joins are the only nodes
// with multiple predecessors.  Targets are the other basic block entries.
// The types enforce edge-split form---joins are forbidden as the successors
// of branches.
class BlockEntryInstr : public Instruction {
 public:
  virtual bool IsBlockEntry() const { return true; }

  static BlockEntryInstr* cast(Instruction* instr) {
    ASSERT(instr->IsBlockEntry());
    return reinterpret_cast<BlockEntryInstr*>(instr);
  }

  intptr_t block_number() const { return block_number_; }
  void set_block_number(intptr_t number) { block_number_ = number; }

 protected:
  BlockEntryInstr() : Instruction(), block_number_(-1) { }

 private:
  intptr_t block_number_;

  DISALLOW_COPY_AND_ASSIGN(BlockEntryInstr);
};


class JoinEntryInstr : public BlockEntryInstr {
 public:
  JoinEntryInstr() : BlockEntryInstr(), successor_(NULL) { }

  DECLARE_INSTRUCTION(JoinEntry)

  virtual void SetSuccessor(Instruction* instr) {
    ASSERT(successor_ == NULL);
    successor_ = instr;
  }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  Instruction* successor_;

  DISALLOW_COPY_AND_ASSIGN(JoinEntryInstr);
};


class TargetEntryInstr : public BlockEntryInstr {
 public:
  TargetEntryInstr() : BlockEntryInstr(), successor_(NULL) {
  }

  DECLARE_INSTRUCTION(TargetEntry)

  virtual void SetSuccessor(Instruction* instr) {
    ASSERT(successor_ == NULL);
    successor_ = instr;
  }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  Instruction* successor_;

  DISALLOW_COPY_AND_ASSIGN(TargetEntryInstr);
};


// The non-optimizing compiler assumes that there is exactly one use of
// every temporary so they can be deallocated at their use.  Some AST nodes,
// e.g., expr0[expr1]++, violate this assumption (there are two uses of each
// of the values expr0 and expr1).
//
// PickTemp is used to name (with 'destination') a copy of a live temporary
// (named 'source') without counting as the use of the source.
class PickTempInstr : public Instruction {
 public:
  PickTempInstr(intptr_t dst, intptr_t src)
      : Instruction(), destination_(dst), source_(src), successor_(NULL) { }

  DECLARE_INSTRUCTION(PickTemp)

  intptr_t destination() const { return destination_; }
  intptr_t source() const { return source_; }

  virtual void SetSuccessor(Instruction* instr) {
    ASSERT(successor_ == NULL && instr != NULL);
    successor_ = instr;
  }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  const intptr_t destination_;
  const intptr_t source_;
  Instruction* successor_;

  DISALLOW_COPY_AND_ASSIGN(PickTempInstr);
};


// The non-optimizing compiler assumes that temporary definitions and uses
// obey a stack discipline, so they can be allocated and deallocated with
// push and pop.  Some Some AST nodes, e.g., expr++, violate this assumption
// (the value expr+1 is produced after the value of expr, and also consumed
// after it).
//
// We 'preallocate' temporaries (named with 'destination') such as the one
// for expr+1 and use TuckTemp to mutate them by overwriting them with a
// copy of a temporary (named with 'source').
class TuckTempInstr : public Instruction {
 public:
  TuckTempInstr(intptr_t dst, intptr_t src)
      : Instruction(), destination_(dst), source_(src), successor_(NULL) { }

  DECLARE_INSTRUCTION(TuckTemp)

  intptr_t destination() const { return destination_; }
  intptr_t source() const { return source_; }

  virtual void SetSuccessor(Instruction* instr) {
    ASSERT(successor_ == NULL && instr != NULL);
    successor_ = instr;
  }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  const intptr_t destination_;
  const intptr_t source_;
  Instruction* successor_;

  DISALLOW_COPY_AND_ASSIGN(TuckTempInstr);
};


class DoInstr : public Instruction {
 public:
  explicit DoInstr(Computation* comp)
      : Instruction(), computation_(comp), successor_(NULL) { }

  DECLARE_INSTRUCTION(Do)

  Computation* computation() const { return computation_; }

  virtual void SetSuccessor(Instruction* instr) {
    ASSERT(successor_ == NULL);
    successor_ = instr;
  }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  Computation* computation_;
  Instruction* successor_;

  DISALLOW_COPY_AND_ASSIGN(DoInstr);
};


class BindInstr : public Instruction {
 public:
  BindInstr(intptr_t temp_index, Computation* computation)
      : Instruction(),
        temp_index_(temp_index),
        computation_(computation),
        successor_(NULL) { }

  DECLARE_INSTRUCTION(Bind)

  intptr_t temp_index() const { return temp_index_; }
  Computation* computation() const { return computation_; }

  virtual void SetSuccessor(Instruction* instr) {
    ASSERT(successor_ == NULL);
    successor_ = instr;
  }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  const intptr_t temp_index_;
  Computation* computation_;
  Instruction* successor_;

  DISALLOW_COPY_AND_ASSIGN(BindInstr);
};


class ReturnInstr : public Instruction {
 public:
  ReturnInstr(Value* value, intptr_t token_index)
      : Instruction(), value_(value), token_index_(token_index) { }

  DECLARE_INSTRUCTION(Return)

  Value* value() const { return value_; }
  intptr_t token_index() const { return token_index_; }

  virtual void SetSuccessor(Instruction* instr) { UNREACHABLE(); }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  Value* value_;
  intptr_t token_index_;

  DISALLOW_COPY_AND_ASSIGN(ReturnInstr);
};


class BranchInstr : public Instruction {
 public:
  explicit BranchInstr(Value* value)
      : Instruction(),
        value_(value),
        true_successor_(NULL),
        false_successor_(NULL) { }

  DECLARE_INSTRUCTION(Branch)

  Value* value() const { return value_; }
  BlockEntryInstr* true_successor() const { return true_successor_; }
  BlockEntryInstr* false_successor() const { return false_successor_; }

  BlockEntryInstr** true_successor_address() { return &true_successor_; }
  BlockEntryInstr** false_successor_address() { return &false_successor_; }

  virtual void SetSuccessor(Instruction* instr) { UNREACHABLE(); }

  virtual void Postorder(GrowableArray<BlockEntryInstr*>* block_entries);

 private:
  Value* value_;
  BlockEntryInstr* true_successor_;
  BlockEntryInstr* false_successor_;

  DISALLOW_COPY_AND_ASSIGN(BranchInstr);
};

#undef DECLARE_INSTRUCTION


// Visitor base class to visit each instruction and computation in a flow
// graph as defined by a reversed list of basic blocks.
class FlowGraphVisitor : public ValueObject {
 public:
  FlowGraphVisitor() { }
  virtual ~FlowGraphVisitor() { }

  // Visit each block in the array list in reverse, and for each block its
  // instructions in order from the block entry to exit.
  virtual void VisitBlocks(const GrowableArray<BlockEntryInstr*>& block_order);

  // Visit functions for instruction and computation classes, with empty
  // default implementations.
#define DECLARE_VISIT_COMPUTATION(ShortName, ClassName)                        \
  virtual void Visit##ShortName(ClassName* comp) { }

#define DECLARE_VISIT_INSTRUCTION(ShortName)                                   \
  virtual void Visit##ShortName(ShortName##Instr* instr) { }

  FOR_EACH_COMPUTATION(DECLARE_VISIT_COMPUTATION)
  FOR_EACH_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_COMPUTATION
#undef DECLARE_VISIT_INSTRUCTION

 private:
  DISALLOW_COPY_AND_ASSIGN(FlowGraphVisitor);
};


}  // namespace dart

#endif  // VM_INTERMEDIATE_LANGUAGE_H_
