// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/flow_graph_builder.h"

#include "vm/ast_printer.h"
#include "vm/flags.h"
#include "vm/intermediate_language.h"
#include "vm/longjump.h"
#include "vm/os.h"
#include "vm/parser.h"

namespace dart {

DEFINE_FLAG(bool, print_flow_graph, false, "Print the IR flow graph.");
DECLARE_FLAG(bool, enable_type_checks);
DECLARE_FLAG(bool, print_ast);

void EffectGraphVisitor::Append(const EffectGraphVisitor& other_fragment) {
  ASSERT(is_open());
  if (other_fragment.is_empty()) return;
  if (is_empty()) {
    entry_ = other_fragment.entry();
    exit_ = other_fragment.exit();
  } else {
    exit()->SetSuccessor(other_fragment.entry());
    exit_ = other_fragment.exit();
  }
}


void EffectGraphVisitor::AddInstruction(Instruction* instruction) {
  ASSERT(is_open());
  if (is_empty()) {
    entry_ = exit_ = instruction;
  } else {
    exit()->SetSuccessor(instruction);
    exit_ = instruction;
  }
}


void EffectGraphVisitor::Join(const TestGraphVisitor& test_fragment,
                              const EffectGraphVisitor& true_fragment,
                              const EffectGraphVisitor& false_fragment) {
  // We have: a test graph fragment with zero, one, or two available exits;
  // and a pair of effect graph fragments with zero or one available exits.
  // We want to append the branch and (if necessary) a join node to this
  // graph fragment.
  ASSERT(is_open());

  // 1. Connect the test to this graph.
  Append(test_fragment);

  // 2. Connect the true and false bodies to the test if they are reachable,
  // and if so record their exits (if any).
  Instruction* true_exit = NULL;
  Instruction* false_exit = NULL;
  if (test_fragment.can_be_true()) {
    TargetEntryInstr* true_entry = new TargetEntryInstr();
    *test_fragment.true_successor_address() = true_entry;
    true_entry->SetSuccessor(true_fragment.entry());
    true_exit = true_fragment.is_empty() ? true_entry : true_fragment.exit();

    TargetEntryInstr* false_entry = new TargetEntryInstr();
    *test_fragment.false_successor_address() = false_entry;
    false_entry->SetSuccessor(false_fragment.entry());
    false_exit =
        false_fragment.is_empty() ? false_entry : false_fragment.exit();
  }

  // 3. Add a join or select one (or neither) of the arms as exit.
  if (true_exit == NULL) {
    exit_ = false_exit;  // May be NULL.
  } else if (false_exit == NULL) {
    exit_ = true_exit;
  } else {
    exit_ = new JoinEntryInstr();
    true_exit->SetSuccessor(exit_);
    false_exit->SetSuccessor(exit_);
  }
}


void EffectGraphVisitor::TieLoop(const TestGraphVisitor& test_fragment,
                                 const EffectGraphVisitor& body_fragment) {
  // We have: a test graph fragment with zero, one, or two available exits;
  // and an effect graph fragment with zero or one available exits.  We want
  // to append the 'while loop' consisting of the test graph fragment as
  // condition and the effect graph fragment as body.
  ASSERT(is_open());

  // 1. Connect the body to the test if it is reachable, and if so record
  // its exit (if any).
  Instruction* body_exit = NULL;
  if (test_fragment.can_be_true()) {
    TargetEntryInstr* body_entry = new TargetEntryInstr();
    *test_fragment.true_successor_address() = body_entry;
    body_entry->SetSuccessor(body_fragment.entry());
    body_exit = body_fragment.is_empty() ? body_entry : body_fragment.exit();
  }

  // 2. Connect the test to this graph, including the body if reachable and
  // using a fresh join node if the body is reachable and has an open exit.
  if (body_exit == NULL) {
    Append(test_fragment);
  } else {
    JoinEntryInstr* join = new JoinEntryInstr();
    AddInstruction(join);
    join->SetSuccessor(test_fragment.entry());
    body_exit->SetSuccessor(join);
  }

  // 3. Set the exit to the graph to be empty or a fresh target node
  // depending on whether the false branch of the test is reachable.
  if (test_fragment.can_be_false()) {
    exit_ = *test_fragment.false_successor_address() = new TargetEntryInstr();
  } else {
    exit_ = NULL;
  }
}


void TestGraphVisitor::ReturnValue(Value* value) {
  BranchInstr* branch = new BranchInstr(value);
  AddInstruction(branch);
  CloseFragment();
  true_successor_address_ = branch->true_successor_address();
  false_successor_address_ = branch->false_successor_address();
}


void ArgumentGraphVisitor::ReturnValue(Value* value) {
  value_ = value;
  if (value->IsConstant()) {
    AddInstruction(new BindInstr(temp_index(), value));
    value_ = new TempVal(AllocateTempIndex());
  }
}


void EffectGraphVisitor::Bailout(const char* reason) {
  owner()->Bailout(reason);
}


// <Statement> ::= Return { value:                <Expression>
//                          inlined_finally_list: <InlinedFinally>* }
void EffectGraphVisitor::VisitReturnNode(ReturnNode* node) {
  ValueGraphVisitor for_value(owner(), temp_index());
  node->value()->Visit(&for_value);
  Append(for_value);

  for (intptr_t i = 0; i < node->inlined_finally_list_length(); i++) {
    EffectGraphVisitor for_effect(owner(), for_value.temp_index());
    node->InlinedFinallyNodeAt(i)->Visit(&for_effect);
    Append(for_effect);
    if (!is_open()) return;
  }

  Value* return_value = for_value.value();
  if (FLAG_enable_type_checks) {
    const RawFunction::Kind kind = owner()->parsed_function().function().kind();
    // Implicit getters do not need a type check at return.
    if ((kind != RawFunction::kImplicitGetter) &&
        (kind != RawFunction::kConstImplicitGetter)) {
      const AbstractType& type =
          AbstractType::ZoneHandle(
              owner()->parsed_function().function().result_type());
      AssertAssignableComp* assert =
          new AssertAssignableComp(return_value, type);
      AddInstruction(new BindInstr(temp_index(), assert));
      return_value = new TempVal(temp_index());
    }
  }

  AddInstruction(new ReturnInstr(return_value, node->token_index()));
  CloseFragment();
}


// <Expression> ::= Literal { literal: Instance }
void EffectGraphVisitor::VisitLiteralNode(LiteralNode* node) {
  return;
}

void ValueGraphVisitor::VisitLiteralNode(LiteralNode* node) {
  ReturnValue(new ConstantVal(node->literal()));
}

void TestGraphVisitor::VisitLiteralNode(LiteralNode* node) {
  ReturnValue(new ConstantVal(node->literal()));
}


// Type nodes only occur as the right-hand side of instanceof comparisons,
// and they are handled specially in that context.
void EffectGraphVisitor::VisitTypeNode(TypeNode* node) { UNREACHABLE(); }


// <Expression> :: Assignable { expr:     <Expression>
//                              type:     AbstractType
//                              dst_name: String }
void EffectGraphVisitor::VisitAssignableNode(AssignableNode* node) {
  ValueGraphVisitor for_value(owner(), temp_index());
  node->expr()->Visit(&for_value);
  Append(for_value);
  AssertAssignableComp* assert =
      new AssertAssignableComp(for_value.value(), node->type());
  ReturnComputation(assert);
}


// <Expression> :: BinaryOp { kind:  Token::Kind
//                            left:  <Expression>
//                            right: <Expression> }
void EffectGraphVisitor::VisitBinaryOpNode(BinaryOpNode* node) {
  // Operators "&&" and "||" cannot be overloaded therefore do not call
  // operator.
  if ((node->kind() == Token::kAND) || (node->kind() == Token::kOR)) {
    // Implement short-circuit logic: do not evaluate right if evaluation
    // of left is sufficient.
    Bailout("EffectGraphVisitor::VisitBinaryOpNode AND/OR");
  }
  ArgumentGraphVisitor for_left_value(owner(), temp_index());
  node->left()->Visit(&for_left_value);
  Append(for_left_value);
  ArgumentGraphVisitor for_right_value(owner(), for_left_value.temp_index());
  node->right()->Visit(&for_right_value);
  Append(for_right_value);
  ZoneGrowableArray<Value*>* arguments = new ZoneGrowableArray<Value*>(2);
  arguments->Add(for_left_value.value());
  arguments->Add(for_right_value.value());
  const String& name = String::ZoneHandle(String::NewSymbol(node->Name()));
  InstanceCallComp* call =
      new InstanceCallComp(node->id(), node->token_index(), name,
                           arguments, Array::ZoneHandle(), 2);
  ReturnComputation(call);
}


void EffectGraphVisitor::VisitStringConcatNode(StringConcatNode* node) {
  Bailout("EffectGraphVisitor::VisitStringConcatNode");
}


// <Expression> :: Comparison { kind:  Token::Kind
//                              left:  <Expression>
//                              right: <Expression> }
void EffectGraphVisitor::VisitComparisonNode(ComparisonNode* node) {
  if (Token::IsInstanceofOperator(node->kind())) {
    ArgumentGraphVisitor for_left_value(owner(), temp_index());
    node->left()->Visit(&for_left_value);
    Append(for_left_value);
    InstanceOfComp* instance_of = new InstanceOfComp(
        node->id(),
        node->token_index(),
        for_left_value.value(),
        node->right()->AsTypeNode()->type(),
        (node->kind() == Token::kISNOT));
    ReturnComputation(instance_of);
    return;
  }
  if ((node->kind() == Token::kEQ_STRICT) ||
      (node->kind() == Token::kNE_STRICT)) {
    ValueGraphVisitor for_left_value(owner(), temp_index());
    node->left()->Visit(&for_left_value);
    Append(for_left_value);
    ValueGraphVisitor for_right_value(owner(), for_left_value.temp_index());
    node->right()->Visit(&for_right_value);
    Append(for_right_value);
    StrictCompareComp* comp = new StrictCompareComp(
        node->kind(), for_left_value.value(), for_right_value.value());
    ReturnComputation(comp);
    return;
  }

  ArgumentGraphVisitor for_left_value(owner(), temp_index());
  node->left()->Visit(&for_left_value);
  Append(for_left_value);
  ArgumentGraphVisitor for_right_value(owner(), for_left_value.temp_index());
  node->right()->Visit(&for_right_value);
  Append(for_right_value);
  ZoneGrowableArray<Value*>* arguments = new ZoneGrowableArray<Value*>(2);
  arguments->Add(for_left_value.value());
  arguments->Add(for_right_value.value());
  // 'kNE' is not overloadable, must implement as kEQ and negation.
  // Boolean negation '!' cannot be overloaded neither.
  if (node->kind() == Token::kNE) {
    const String& name = String::ZoneHandle(String::NewSymbol("=="));
    InstanceCallComp* call_equal =
        new InstanceCallComp(node->id(), node->token_index(), name,
                             arguments, Array::ZoneHandle(), 2);
    AddInstruction(new BindInstr(temp_index(), call_equal));
    Value* eq_result = new TempVal(temp_index());
    if (FLAG_enable_type_checks) {
      Bailout("GenerateConditionTypeCheck in kNE");
    }
    BooleanNegateComp* negate = new BooleanNegateComp(eq_result);
    ReturnComputation(negate);
  } else {
    const String& name = String::ZoneHandle(String::NewSymbol(node->Name()));
    InstanceCallComp* call =
        new InstanceCallComp(node->id(), node->token_index(), name,
                             arguments, Array::ZoneHandle(), 2);
    ReturnComputation(call);
  }
}


void EffectGraphVisitor::VisitUnaryOpNode(UnaryOpNode* node) {
  // "!" cannot be overloaded, therefore do not call operator.
  if (node->kind() == Token::kNOT) {
    ValueGraphVisitor for_value(owner(), temp_index());
    node->operand()->Visit(&for_value);
    Append(for_value);
    if (FLAG_enable_type_checks) {
      Bailout("GenerateConditionTypeCheck in kNOT");
    }
    BooleanNegateComp* negate = new BooleanNegateComp(for_value.value());
    ReturnComputation(negate);
    return;
  }
  ArgumentGraphVisitor for_value(owner(), temp_index());
  node->operand()->Visit(&for_value);
  Append(for_value);
  ZoneGrowableArray<Value*>* arguments = new ZoneGrowableArray<Value*>(1);
  arguments->Add(for_value.value());
  const String& name =
      String::ZoneHandle(String::NewSymbol((node->kind() == Token::kSUB)
                                               ? Token::Str(Token::kNEGATE)
                                               : node->Name()));
  InstanceCallComp* call =
      new InstanceCallComp(node->id(), node->token_index(), name,
                           arguments, Array::ZoneHandle(), 1);
  ReturnComputation(call);
}


void EffectGraphVisitor::VisitIncrOpLocalNode(IncrOpLocalNode* node) {
  Bailout("EffectGraphVisitor::VisitIncrOpLocalNode");
}


void EffectGraphVisitor::VisitIncrOpInstanceFieldNode(
    IncrOpInstanceFieldNode* node) {
  Bailout("EffectGraphVisitor::VisitIncrOpInstanceFieldNode");
}


void EffectGraphVisitor::VisitIncrOpIndexedNode(IncrOpIndexedNode* node) {
  ASSERT((node->kind() == Token::kINCR) || (node->kind() == Token::kDECR));
  if (node->prefix()) {
    Bailout("IncrOpIndexed prefix");
  } else {
    // Leave a placeholder, evaluate receiver and index.
    //   t0 <- #0
    //   t1 <- ... receiver ...
    //   t2 <- ... index ...
    const Smi& placeholder = Smi::ZoneHandle(Smi::New(0));
    const int placeholder_index = temp_index();
    AddInstruction(new BindInstr(placeholder_index,
                                 new ConstantVal(placeholder)));

    ArgumentGraphVisitor for_array(owner(), temp_index() + 1);
    node->array()->Visit(&for_array);
    Append(for_array);
    ASSERT(for_array.value()->IsTemp());
    const int array_index = for_array.value()->AsTemp()->index();

    ArgumentGraphVisitor for_index(owner(), for_array.temp_index());
    node->index()->Visit(&for_index);
    Append(for_index);
    ASSERT(for_index.value()->IsTemp());
    const int index_index = for_index.value()->AsTemp()->index();

    // Duplicate the receiver and index values, load the value.
    //   t3 <- Pick(t1)
    //   t4 <- Pick(t2)
    //   t3 <- InstanceCall([], t3, t4)
    int next_index = for_index.temp_index();
    AddInstruction(new PickTempInstr(next_index, array_index));
    AddInstruction(new PickTempInstr(next_index + 1, index_index));
    ZoneGrowableArray<Value*>* arguments = new ZoneGrowableArray<Value*>(2);
    arguments->Add(new TempVal(next_index));
    arguments->Add(new TempVal(next_index + 1));
    const String& load_name =
        String::ZoneHandle(String::NewSymbol(Token::Str(Token::kINDEX)));
    InstanceCallComp* load =
        new InstanceCallComp(node->load_id(), node->token_index(), load_name,
                             arguments, Array::ZoneHandle(), 1);
    AddInstruction(new BindInstr(next_index, load));

    // Preserve the original value and then increment.
    //   t0 := t3
    //   t4 <- #1
    //   t3 <- InstanceCall(op, t3, t4)
    AddInstruction(new TuckTempInstr(placeholder_index, next_index));
    const Smi& one = Smi::ZoneHandle(Smi::New(1));
    AddInstruction(new BindInstr(next_index + 1, new ConstantVal(one)));
    arguments = new ZoneGrowableArray<Value*>(2);
    arguments->Add(new TempVal(next_index));
    arguments->Add(new TempVal(next_index + 1));
    const String& op_name = String::ZoneHandle(String::NewSymbol(
        (node->kind() == Token::kINCR) ? "+" : "-"));
    InstanceCallComp* add =
        new InstanceCallComp(node->operator_id(), node->token_index(), op_name,
                             arguments, Array::ZoneHandle(), 2);
    AddInstruction(new BindInstr(next_index, add));

    // Perform the store.
    //  InstanceCallComp([]=, t1, t2, t3)
    //  ... value is t0 ...
    arguments = new ZoneGrowableArray<Value*>(3);
    arguments->Add(for_array.value());
    arguments->Add(for_index.value());
    arguments->Add(new TempVal(next_index));
    const String& store_name =
        String::ZoneHandle(String::NewSymbol(Token::Str(Token::kASSIGN_INDEX)));
    InstanceCallComp* store =
        new InstanceCallComp(node->store_id(), node->token_index(), store_name,
                             arguments, Array::ZoneHandle(), 1);
    AddInstruction(new DoInstr(store));

    ReturnValue(new TempVal(AllocateTempIndex()));
  }
}


void EffectGraphVisitor::VisitConditionalExprNode(ConditionalExprNode* node) {
  Bailout("EffectGraphVisitor::VisitConditionalExprNode");
}


// <Statement> ::= If { condition: <Expression>
//                      true_branch: <Sequence>
//                      false_branch: <Sequence> }
void EffectGraphVisitor::VisitIfNode(IfNode* node) {
  TestGraphVisitor for_test(owner(), temp_index());
  node->condition()->Visit(&for_test);

  EffectGraphVisitor for_true(owner(), temp_index());
  EffectGraphVisitor for_false(owner(), temp_index());

  if (for_test.can_be_true()) {
    node->true_branch()->Visit(&for_true);
    // The for_false graph fragment will be empty (default graph fragment)
    // if we do not call Visit.
    if (node->false_branch() != NULL) node->false_branch()->Visit(&for_false);
  }
  Join(for_test, for_true, for_false);
}


void EffectGraphVisitor::VisitSwitchNode(SwitchNode* node) {
  Bailout("EffectGraphVisitor::VisitSwitchNode");
}


void EffectGraphVisitor::VisitCaseNode(CaseNode* node) {
  Bailout("EffectGraphVisitor::VisitCaseNode");
}


// <Statement> ::= While { label:     SourceLabel
//                         condition: <Expression>
//                         body:      <Sequence> }
void EffectGraphVisitor::VisitWhileNode(WhileNode* node) {
  TestGraphVisitor for_test(owner(), temp_index());
  node->condition()->Visit(&for_test);

  EffectGraphVisitor for_body(owner(), temp_index());
  if (for_test.can_be_true()) node->body()->Visit(&for_body);
  TieLoop(for_test, for_body);
}


void EffectGraphVisitor::VisitDoWhileNode(DoWhileNode* node) {
  EffectGraphVisitor for_body(owner(), temp_index());
  node->body()->Visit(&for_body);
  TestGraphVisitor for_test(owner(), temp_index());
  node->condition()->Visit(&for_test);
  ASSERT(is_open());

  // Tie do-while loop (test is after the body).
  JoinEntryInstr* join = new JoinEntryInstr();
  AddInstruction(join);
  join->SetSuccessor(for_body.entry());
  Instruction* body_exit = for_body.is_empty() ? join : for_body.exit();

  if (body_exit != NULL) {
    TargetEntryInstr* target_entry = new TargetEntryInstr();
    target_entry->SetSuccessor(for_test.entry());
    body_exit->SetSuccessor(target_entry);
  }

  *for_test.true_successor_address() = join;
  exit_ = *for_test.false_successor_address() = new TargetEntryInstr();
}


void EffectGraphVisitor::VisitForNode(ForNode* node) {
  Bailout("EffectGraphVisitor::VisitForNode");
}


void EffectGraphVisitor::VisitJumpNode(JumpNode* node) {
  Bailout("EffectGraphVisitor::VisitJumpNode");
}


void EffectGraphVisitor::VisitArgumentListNode(ArgumentListNode* node) {
  UNREACHABLE();
}


void EffectGraphVisitor::VisitArrayNode(ArrayNode* node) {
  Bailout("EffectGraphVisitor::VisitArrayNode");
}


void EffectGraphVisitor::VisitClosureNode(ClosureNode* node) {
  Bailout("EffectGraphVisitor::VisitClosureNode");
}


void EffectGraphVisitor::TranslateArgumentList(
    const ArgumentListNode& node,
    intptr_t next_temp_index,
    ZoneGrowableArray<Value*>* values) {
  for (intptr_t i = 0; i < node.length(); ++i) {
    ArgumentGraphVisitor for_argument(owner(), next_temp_index);
    node.NodeAt(i)->Visit(&for_argument);
    Append(for_argument);
    next_temp_index = for_argument.temp_index();
    values->Add(for_argument.value());
  }
}

void EffectGraphVisitor::VisitInstanceCallNode(InstanceCallNode* node) {
  ArgumentListNode* arguments = node->arguments();
  int length = arguments->length();
  ZoneGrowableArray<Value*>* values = new ZoneGrowableArray<Value*>(length + 1);

  ArgumentGraphVisitor for_receiver(owner(), temp_index());
  node->receiver()->Visit(&for_receiver);
  Append(for_receiver);
  values->Add(for_receiver.value());

  TranslateArgumentList(*arguments, for_receiver.temp_index(), values);
  InstanceCallComp* call =
      new InstanceCallComp(node->id(), node->token_index(),
                           node->function_name(), values,
                           arguments->names(), 1);
  ReturnComputation(call);
}


// <Expression> ::= StaticCall { function: Function
//                               arguments: <ArgumentList> }
void EffectGraphVisitor::VisitStaticCallNode(StaticCallNode* node) {
  int length = node->arguments()->length();
  ZoneGrowableArray<Value*>* values = new ZoneGrowableArray<Value*>(length);
  TranslateArgumentList(*node->arguments(), temp_index(), values);
  StaticCallComp* call = new StaticCallComp(node, values);
  ReturnComputation(call);
}


void EffectGraphVisitor::VisitClosureCallNode(ClosureCallNode* node) {
  Bailout("EffectGraphVisitor::VisitClosureCallNode");
}


void EffectGraphVisitor::VisitCloneContextNode(CloneContextNode* node) {
  Bailout("EffectGraphVisitor::VisitCloneContextNode");
}


void EffectGraphVisitor::VisitConstructorCallNode(ConstructorCallNode* node) {
  Bailout("EffectGraphVisitor::VisitConstructorCallNode");
}


void EffectGraphVisitor::VisitInstanceGetterNode(InstanceGetterNode* node) {
  ArgumentGraphVisitor for_receiver(owner(), temp_index());
  node->receiver()->Visit(&for_receiver);
  Append(for_receiver);
  ZoneGrowableArray<Value*>* arguments = new ZoneGrowableArray<Value*>(1);
  arguments->Add(for_receiver.value());
  const String& name =
      String::ZoneHandle(Field::GetterSymbol(node->field_name()));
  InstanceCallComp* call =
      new InstanceCallComp(node->id(), node->token_index(), name,
                           arguments, Array::ZoneHandle(), 1);
  ReturnComputation(call);
}


void EffectGraphVisitor::VisitInstanceSetterNode(InstanceSetterNode* node) {
  ArgumentGraphVisitor for_receiver(owner(), temp_index());
  node->receiver()->Visit(&for_receiver);
  Append(for_receiver);
  ArgumentGraphVisitor for_value(owner(), for_receiver.temp_index());
  node->value()->Visit(&for_value);
  Append(for_value);
  InstanceSetterComp* setter = new InstanceSetterComp(node,
                                                      for_receiver.value(),
                                                      for_value.value());
  ReturnComputation(setter);
}


void EffectGraphVisitor::VisitStaticGetterNode(StaticGetterNode* node) {
  Bailout("EffectGraphVisitor::VisitStaticGetterNode");
}


void EffectGraphVisitor::VisitStaticSetterNode(StaticSetterNode* node) {
  Bailout("EffectGraphVisitor::VisitStaticSetterNode");
}


void EffectGraphVisitor::VisitNativeBodyNode(NativeBodyNode* node) {
  NativeCallComp* native_call = new NativeCallComp(node);
  ReturnComputation(native_call);
}


void EffectGraphVisitor::VisitPrimaryNode(PrimaryNode* node) {
  Bailout("EffectGraphVisitor::VisitPrimaryNode");
}


// <Expression> ::= LoadLocal { local: LocalVariable }
void EffectGraphVisitor::VisitLoadLocalNode(LoadLocalNode* node) {
  return;
}

void ValueGraphVisitor::VisitLoadLocalNode(LoadLocalNode* node) {
  LoadLocalComp* load = new LoadLocalComp(node->local());
  ReturnComputation(load);
}

void TestGraphVisitor::VisitLoadLocalNode(LoadLocalNode* node) {
  LoadLocalComp* load = new LoadLocalComp(node->local());
  ReturnComputation(load);
}


// <Expression> ::= StoreLocal { local: LocalVariable
//                               value: <Expression> }
void EffectGraphVisitor::VisitStoreLocalNode(StoreLocalNode* node) {
  ValueGraphVisitor for_value(owner(), temp_index());
  node->value()->Visit(&for_value);
  Append(for_value);

  Value* value = for_value.value();
  if (FLAG_enable_type_checks) {
    AssertAssignableComp* assert =
        new AssertAssignableComp(value, node->local().type());
    AddInstruction(new BindInstr(temp_index(), assert));
    value = new TempVal(temp_index());
  }

  StoreLocalComp* store = new StoreLocalComp(node->local(), value);
  ReturnComputation(store);
}


void EffectGraphVisitor::VisitLoadInstanceFieldNode(
    LoadInstanceFieldNode* node) {
  ValueGraphVisitor for_instance(owner(), temp_index());
  node->instance()->Visit(&for_instance);
  Append(for_instance);
  LoadInstanceFieldComp* load =
      new LoadInstanceFieldComp(node, for_instance.value());
  ReturnComputation(load);
}


void EffectGraphVisitor::VisitStoreInstanceFieldNode(
    StoreInstanceFieldNode* node) {
  ValueGraphVisitor for_instance(owner(), temp_index());
  node->instance()->Visit(&for_instance);
  Append(for_instance);
  ValueGraphVisitor for_value(owner(), for_instance.temp_index());
  node->value()->Visit(&for_value);
  Append(for_value);
  Value* store_value = for_value.value();
  if (FLAG_enable_type_checks) {
    const AbstractType& type = AbstractType::ZoneHandle(node->field().type());
    AssertAssignableComp* assert = new AssertAssignableComp(store_value, type);
    AddInstruction(new BindInstr(temp_index(), assert));
    store_value = new TempVal(temp_index());
  }
  StoreInstanceFieldComp* store =
      new StoreInstanceFieldComp(node, for_instance.value(), store_value);
  ReturnComputation(store);
}


void EffectGraphVisitor::VisitLoadStaticFieldNode(LoadStaticFieldNode* node) {
  LoadStaticFieldComp* load = new LoadStaticFieldComp(node->field());
  ReturnComputation(load);
}


void EffectGraphVisitor::VisitStoreStaticFieldNode(StoreStaticFieldNode* node) {
  ValueGraphVisitor for_value(owner(), temp_index());
  node->value()->Visit(&for_value);
  Append(for_value);
  Value* store_value = for_value.value();
  if (FLAG_enable_type_checks) {
    const AbstractType& type = AbstractType::ZoneHandle(node->field().type());
    AssertAssignableComp* assert = new AssertAssignableComp(store_value, type);
    AddInstruction(new BindInstr(temp_index(), assert));
    store_value = new TempVal(temp_index());
  }
  StoreStaticFieldComp* store =
      new StoreStaticFieldComp(node, store_value);
  ReturnComputation(store);
}


void EffectGraphVisitor::VisitLoadIndexedNode(LoadIndexedNode* node) {
  ArgumentGraphVisitor for_array(owner(), temp_index());
  node->array()->Visit(&for_array);
  Append(for_array);
  ArgumentGraphVisitor for_index(owner(), for_array.temp_index());
  node->index_expr()->Visit(&for_index);
  Append(for_index);
  ZoneGrowableArray<Value*>* arguments = new ZoneGrowableArray<Value*>(2);
  arguments->Add(for_array.value());
  arguments->Add(for_index.value());
  const String& name =
      String::ZoneHandle(String::NewSymbol(Token::Str(Token::kINDEX)));
  InstanceCallComp* call =
      new InstanceCallComp(node->id(), node->token_index(), name,
                           arguments, Array::ZoneHandle(), 1);
  ReturnComputation(call);
}


void EffectGraphVisitor::VisitStoreIndexedNode(StoreIndexedNode* node) {
  ArgumentGraphVisitor for_array(owner(), temp_index());
  node->array()->Visit(&for_array);
  Append(for_array);
  ArgumentGraphVisitor for_index(owner(), for_array.temp_index());
  node->index_expr()->Visit(&for_index);
  Append(for_index);
  ArgumentGraphVisitor for_value(owner(), for_index.temp_index());
  node->value()->Visit(&for_value);
  Append(for_value);
  StoreIndexedComp* store = new StoreIndexedComp(node,
                                                 for_array.value(),
                                                 for_index.value(),
                                                 for_value.value());
  ReturnComputation(store);
}


// <Statement> ::= Sequence { scope: LocalScope
//                            nodes: <Statement>*
//                            label: SourceLabel }
void EffectGraphVisitor::VisitSequenceNode(SequenceNode* node) {
  if ((node->scope() != NULL) &&
      (node->scope()->num_context_variables() != 0)) {
    Bailout("Sequence needs a context.  Gotta have a context.");
  }
  intptr_t i = 0;
  while (is_open() && (i < node->length())) {
    EffectGraphVisitor for_effect(owner(), temp_index());
    node->NodeAt(i++)->Visit(&for_effect);
    Append(for_effect);
  }
}


void EffectGraphVisitor::VisitCatchClauseNode(CatchClauseNode* node) {
  Bailout("EffectGraphVisitor::VisitCatchClauseNode");
}


void EffectGraphVisitor::VisitTryCatchNode(TryCatchNode* node) {
  Bailout("EffectGraphVisitor::VisitTryCatchNode");
}


void EffectGraphVisitor::VisitThrowNode(ThrowNode* node) {
  Bailout("EffectGraphVisitor::VisitThrowNode");
}


void EffectGraphVisitor::VisitInlinedFinallyNode(InlinedFinallyNode* node) {
  Bailout("EffectGraphVisitor::VisitInlinedFinallyNode");
}


// Graph printing.
class FlowGraphPrinter : public FlowGraphVisitor {
 public:
  explicit FlowGraphPrinter(const Function& function) : function_(function) { }

  virtual ~FlowGraphPrinter() {}

  // Print the instructions in a block terminated by newlines.  Add "goto N"
  // to the end of the block if it ends with an unconditional jump to
  // another block and that block is not next in reverse postorder.
  void VisitBlocks(const GrowableArray<BlockEntryInstr*>& block_order);

  // Visiting a computation prints it with no indentation or newline.
#define DECLARE_VISIT_COMPUTATION(ShortName, ClassName)                        \
  virtual void Visit##ShortName(ClassName* comp);

  // Visiting an instruction prints it with a four space indent and no
  // trailing newline.  Basic block entries are labeled with their block
  // number.
#define DECLARE_VISIT_INSTRUCTION(ShortName)                                   \
  virtual void Visit##ShortName(ShortName##Instr* instr);

  FOR_EACH_COMPUTATION(DECLARE_VISIT_COMPUTATION)
  FOR_EACH_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)

#undef DECLARE_VISIT_COMPUTATION
#undef DECLARE_VISIT_INSTRUCTION

 private:
  const Function& function_;

  DISALLOW_COPY_AND_ASSIGN(FlowGraphPrinter);
};


void FlowGraphPrinter::VisitBlocks(
    const GrowableArray<BlockEntryInstr*>& block_order) {
  OS::Print("==== %s\n", function_.ToFullyQualifiedCString());

  for (intptr_t i = block_order.length() - 1; i >= 0; --i) {
    // Print the block entry.
    Instruction* current = block_order[i]->Accept(this);
    // And all the successors until an exit, branch, or a block entry.
    while ((current != NULL) && !current->IsBlockEntry()) {
      OS::Print("\n");
      current = current->Accept(this);
    }
    if ((current != NULL) && current->IsBlockEntry()) {
      OS::Print(" goto %d", BlockEntryInstr::cast(current)->block_number());
    }
    OS::Print("\n");
  }
}


void FlowGraphPrinter::VisitTemp(TempVal* val) {
  OS::Print("t%d", val->index());
}


void FlowGraphPrinter::VisitConstant(ConstantVal* val) {
  OS::Print("#%s", val->instance().ToCString());
}


void FlowGraphPrinter::VisitAssertAssignable(AssertAssignableComp* comp) {
  OS::Print("AssertAssignable(");
  comp->value()->Accept(this);
  OS::Print(", %s)", comp->type().ToCString());
}


void FlowGraphPrinter::VisitInstanceCall(InstanceCallComp* comp) {
  OS::Print("InstanceCall(%s", comp->function_name().ToCString());
  for (int i = 0; i < comp->ArgumentCount(); ++i) {
    OS::Print(", ");
    comp->ArgumentAt(i)->Accept(this);
  }
  OS::Print(")");
}


void FlowGraphPrinter::VisitStrictCompare(StrictCompareComp* comp) {
  OS::Print("StrictCompare(%s, ", Token::Str(comp->kind()));
  comp->left()->Accept(this);
  OS::Print(", ");
  comp->right()->Accept(this);
  OS::Print(")");
}



void FlowGraphPrinter::VisitStaticCall(StaticCallComp* comp) {
  OS::Print("StaticCall(%s",
            String::Handle(comp->function().name()).ToCString());
  for (int i = 0; i < comp->ArgumentCount(); ++i) {
    OS::Print(", ");
    comp->ArgumentAt(i)->Accept(this);
  }
  OS::Print(")");
}


void FlowGraphPrinter::VisitLoadLocal(LoadLocalComp* comp) {
  OS::Print("LoadLocal(%s)", comp->local().name().ToCString());
}


void FlowGraphPrinter::VisitStoreLocal(StoreLocalComp* comp) {
  OS::Print("StoreLocal(%s, ", comp->local().name().ToCString());
  comp->value()->Accept(this);
  OS::Print(")");
}


void FlowGraphPrinter::VisitNativeCall(NativeCallComp* comp) {
  OS::Print("NativeCall(%s)", comp->native_name().ToCString());
}


void FlowGraphPrinter::VisitLoadInstanceField(LoadInstanceFieldComp* comp) {
  OS::Print("LoadInstanceField(%s, ",
      String::Handle(comp->field().name()).ToCString());
  comp->instance()->Accept(this);
  OS::Print(")");
}


void FlowGraphPrinter::VisitStoreInstanceField(StoreInstanceFieldComp* comp) {
  OS::Print("StoreInstanceField(%s, ",
      String::Handle(comp->field().name()).ToCString());
  comp->instance()->Accept(this);
  OS::Print(", ");
  comp->value()->Accept(this);
  OS::Print(")");
}


void FlowGraphPrinter::VisitLoadStaticField(LoadStaticFieldComp* comp) {
  OS::Print("LoadStaticField(%s)",
      String::Handle(comp->field().name()).ToCString());
}


void FlowGraphPrinter::VisitStoreStaticField(StoreStaticFieldComp* comp) {
  OS::Print("StoreStaticField(%s, ",
      String::Handle(comp->field().name()).ToCString());
  comp->value()->Accept(this);
  OS::Print(")");
}


void FlowGraphPrinter::VisitStoreIndexed(StoreIndexedComp* comp) {
  OS::Print("StoreIndexed(");
  comp->array()->Accept(this);
  OS::Print(", ");
  comp->index()->Accept(this);
  OS::Print(", ");
  comp->value()->Accept(this);
  OS::Print(")");
}


void FlowGraphPrinter::VisitInstanceSetter(InstanceSetterComp* comp) {
  OS::Print("InstanceSetter(");
  comp->receiver()->Accept(this);
  OS::Print(", ");
  comp->value()->Accept(this);
  OS::Print(")");
}


void FlowGraphPrinter::VisitBooleanNegate(BooleanNegateComp* comp) {
  OS::Print("! ");
  comp->value()->Accept(this);
}


void FlowGraphPrinter::VisitInstanceOf(InstanceOfComp* comp) {
  comp->value()->Accept(this);
  OS::Print(" %s %s",
      comp->negate_result() ? "ISNOT" : "IS",
      String::Handle(comp->type().Name()).ToCString());
}

void FlowGraphPrinter::VisitJoinEntry(JoinEntryInstr* instr) {
  OS::Print("%2d: [join]", instr->block_number());
}


void FlowGraphPrinter::VisitTargetEntry(TargetEntryInstr* instr) {
  OS::Print("%2d: [target]", instr->block_number());
}


void FlowGraphPrinter::VisitPickTemp(PickTempInstr* instr) {
  OS::Print("    t%d <- Pick(t%d)", instr->destination(), instr->source());
}


void FlowGraphPrinter::VisitTuckTemp(TuckTempInstr* instr) {
  OS::Print("    t%d := t%d", instr->destination(), instr->source());
}


void FlowGraphPrinter::VisitDo(DoInstr* instr) {
  OS::Print("    ");
  instr->computation()->Accept(this);
}


void FlowGraphPrinter::VisitBind(BindInstr* instr) {
  OS::Print("    t%d <-", instr->temp_index());
  instr->computation()->Accept(this);
}


void FlowGraphPrinter::VisitReturn(ReturnInstr* instr) {
  OS::Print("    return ");
  instr->value()->Accept(this);
}


void FlowGraphPrinter::VisitBranch(BranchInstr* instr) {
  OS::Print("    if ");
  instr->value()->Accept(this);
  OS::Print(" goto(%d, %d)", instr->true_successor()->block_number(),
            instr->false_successor()->block_number());
}


void FlowGraphBuilder::BuildGraph() {
  if (FLAG_print_ast) {
    // Print the function ast before IL generation.
    AstPrinter::PrintFunctionNodes(parsed_function_);
  }
  EffectGraphVisitor for_effect(this, 0);
  for_effect.AddInstruction(new TargetEntryInstr());
  parsed_function().node_sequence()->Visit(&for_effect);
  // Check that the graph is properly terminated.
  ASSERT(!for_effect.is_open());
  if (for_effect.entry() != NULL) {
    // Accumulate basic block entries via postorder traversal.
    for_effect.entry()->Postorder(&postorder_block_entries_);
    // Number the blocks in reverse postorder starting with 0.
    intptr_t last_index = postorder_block_entries_.length() - 1;
    for (intptr_t i = last_index; i >= 0; --i) {
      postorder_block_entries_[i]->set_block_number(last_index - i);
    }
  }
  if (FLAG_print_flow_graph) {
    FlowGraphPrinter printer(parsed_function().function());
    printer.VisitBlocks(postorder_block_entries_);
  }
}


void FlowGraphBuilder::Bailout(const char* reason) {
  const char* kFormat = "FlowGraphBuilder Bailout: %s %s";
  const char* function_name = parsed_function_.function().ToCString();
  intptr_t len = OS::SNPrint(NULL, 0, kFormat, function_name, reason) + 1;
  char* chars = reinterpret_cast<char*>(
      Isolate::Current()->current_zone()->Allocate(len));
  OS::SNPrint(chars, len, kFormat, function_name, reason);
  const Error& error = Error::Handle(
      LanguageError::New(String::Handle(String::New(chars))));
  Isolate::Current()->long_jump_base()->Jump(1, error);
}


}  // namespace dart
