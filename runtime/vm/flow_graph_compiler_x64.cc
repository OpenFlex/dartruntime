// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_X64.
#if defined(TARGET_ARCH_X64)

#include "vm/flow_graph_compiler.h"

#include "vm/ast_printer.h"
#include "vm/code_generator.h"
#include "vm/disassembler.h"
#include "vm/longjump.h"
#include "vm/parser.h"
#include "vm/stub_code.h"

namespace dart {

DECLARE_FLAG(bool, print_ast);
DECLARE_FLAG(bool, print_scopes);
DECLARE_FLAG(bool, trace_functions);

FlowGraphCompiler::FlowGraphCompiler(
    Assembler* assembler,
    const ParsedFunction& parsed_function,
    const GrowableArray<BlockEntryInstr*>* blocks)
    : assembler_(assembler),
      parsed_function_(parsed_function),
      blocks_(blocks),
      block_info_(blocks->length()),
      current_block_(NULL),
      pc_descriptors_list_(new CodeGenerator::DescriptorList()),
      stack_local_count_(0) {
  for (int i = 0; i < blocks->length(); ++i) {
    block_info_.Add(new BlockInfo());
  }
}


FlowGraphCompiler::~FlowGraphCompiler() {
  // BlockInfos are zone-allocated, so their destructors are not called.
  // Verify the labels explicitly here.
  for (int i = 0; i < block_info_.length(); ++i) {
    ASSERT(!block_info_[i]->label.IsLinked());
    ASSERT(!block_info_[i]->label.HasNear());
  }
}


void FlowGraphCompiler::Bailout(const char* reason) {
  const char* kFormat = "FlowGraphCompiler Bailout: %s %s.";
  const char* function_name = parsed_function_.function().ToCString();
  intptr_t len = OS::SNPrint(NULL, 0, kFormat, function_name, reason) + 1;
  char* chars = reinterpret_cast<char*>(
      Isolate::Current()->current_zone()->Allocate(len));
  OS::SNPrint(chars, len, kFormat, function_name, reason);
  const Error& error = Error::Handle(
      LanguageError::New(String::Handle(String::New(chars))));
  Isolate::Current()->long_jump_base()->Jump(1, error);
}

#define __ assembler_->


void FlowGraphCompiler::GenerateAssertAssignable(intptr_t node_id,
                                                 intptr_t token_index,
                                                 const AbstractType& dst_type,
                                                 const String& dst_name) {
  Bailout("GenerateAssertAssignable");
}


void FlowGraphCompiler::LoadValue(Value* value) {
  if (value->IsConstant()) {
    ConstantVal* constant = value->AsConstant();
    if (constant->instance().IsSmi()) {
      int64_t imm = reinterpret_cast<int64_t>(constant->instance().raw());
      __ movq(RAX, Immediate(imm));
    } else {
      __ LoadObject(RAX, value->AsConstant()->instance());
    }
  } else {
    ASSERT(value->IsTemp());
    __ popq(RAX);
  }
}


void FlowGraphCompiler::VisitTemp(TempVal* val) {
  LoadValue(val);
}


void FlowGraphCompiler::VisitConstant(ConstantVal* val) {
  LoadValue(val);
}


void FlowGraphCompiler::VisitAssertAssignable(AssertAssignableComp* comp) {
  Bailout("AssertAssignableComp");
}


// True iff. the arguments to a call will be properly pushed and can
// be popped after the call.
template <typename T> static bool VerifyCallComputation(T* comp) {
  // Argument values should be consecutive temps.
  //
  // TODO(kmillikin): implement stack height tracking so we can also assert
  // they are on top of the stack.
  intptr_t previous = -1;
  for (int i = 0; i < comp->ArgumentCount(); ++i) {
    TempVal* temp = comp->ArgumentAt(i)->AsTemp();
    if (temp == NULL) return false;
    if (i != 0) {
      if (temp->index() != previous + 1) return false;
    }
    previous = temp->index();
  }
  return true;
}


// Truee iff. the v2 is above v1 on stack, or one of them is constant.
static bool VerifyValues(Value* v1, Value* v2) {
  if (v1->IsTemp() && v2->IsTemp()) {
    return (v1->AsTemp()->index() + 1) == v2->AsTemp()->index();
  }
  return true;
}


void FlowGraphCompiler::EmitInstanceCall(intptr_t node_id,
                                         intptr_t token_index,
                                         const String& function_name,
                                         intptr_t argument_count,
                                         const Array& argument_names,
                                         intptr_t checked_argument_count) {
  ICData& ic_data =
      ICData::ZoneHandle(ICData::New(parsed_function_.function(),
                                     function_name,
                                     node_id,
                                     checked_argument_count));
  const Array& arguments_descriptor =
      CodeGenerator::ArgumentsDescriptor(argument_count, argument_names);
  __ LoadObject(RBX, ic_data);
  __ LoadObject(R10, arguments_descriptor);

  uword label_address = 0;
  switch (checked_argument_count) {
    case 1:
      label_address = StubCode::OneArgCheckInlineCacheEntryPoint();
      break;
    case 2:
      label_address = StubCode::TwoArgsCheckInlineCacheEntryPoint();
      break;
    default:
      UNIMPLEMENTED();
  }
  ExternalLabel target_label("InlineCache", label_address);
  __ call(&target_label);
  AddCurrentDescriptor(PcDescriptors::kIcCall, node_id, token_index);
  __ addq(RSP, Immediate(argument_count * kWordSize));
}


void FlowGraphCompiler::VisitInstanceCall(InstanceCallComp* comp) {
  ASSERT(VerifyCallComputation(comp));
  EmitInstanceCall(comp->node_id(),
                   comp->token_index(),
                   comp->function_name(),
                   comp->ArgumentCount(),
                   comp->argument_names(),
                   comp->checked_argument_count());
}


void FlowGraphCompiler::VisitStrictCompare(StrictCompareComp* comp) {
  const Bool& bool_true = Bool::ZoneHandle(Bool::True());
  const Bool& bool_false = Bool::ZoneHandle(Bool::False());
  LoadValue(comp->left());
  __ movq(RDX, RAX);
  LoadValue(comp->right());
  __ cmpq(RAX, RDX);
  Label load_true, done;
  if (comp->kind() == Token::kEQ_STRICT) {
    __ j(EQUAL, &load_true, Assembler::kNearJump);
  } else {
    __ j(NOT_EQUAL, &load_true, Assembler::kNearJump);
  }
  __ LoadObject(RAX, bool_false);
  __ jmp(&done, Assembler::kNearJump);
  __ Bind(&load_true);
  __ LoadObject(RAX, bool_true);
  __ Bind(&done);
}



void FlowGraphCompiler::VisitStaticCall(StaticCallComp* comp) {
  ASSERT(VerifyCallComputation(comp));

  int argument_count = comp->ArgumentCount();
  const Array& arguments_descriptor =
      CodeGenerator::ArgumentsDescriptor(argument_count,
                                         comp->argument_names());
  __ LoadObject(RBX, comp->function());
  __ LoadObject(R10, arguments_descriptor);

  GenerateCall(comp->token_index(),
               &StubCode::CallStaticFunctionLabel(),
               PcDescriptors::kFuncCall);
  __ addq(RSP, Immediate(argument_count * kWordSize));
}


void FlowGraphCompiler::VisitLoadLocal(LoadLocalComp* comp) {
  if (comp->local().is_captured()) {
    Bailout("load of context variable");
  }
  __ movq(RAX, Address(RBP, comp->local().index() * kWordSize));
}


void FlowGraphCompiler::VisitStoreLocal(StoreLocalComp* comp) {
  if (comp->local().is_captured()) {
    Bailout("store to context variable");
  }
  LoadValue(comp->value());
  __ movq(Address(RBP, comp->local().index() * kWordSize), RAX);
}


void FlowGraphCompiler::VisitNativeCall(NativeCallComp* comp) {
  // Push the result place holder initialized to NULL.
  __ PushObject(Object::ZoneHandle());
  // Pass a pointer to the first argument in RAX.
  if (!comp->has_optional_parameters()) {
    __ leaq(RAX, Address(RBP, (1 + comp->argument_count()) * kWordSize));
  } else {
    __ leaq(RAX, Address(RBP, -1 * kWordSize));
  }
  __ movq(RBX, Immediate(reinterpret_cast<uword>(comp->native_c_function())));
  __ movq(R10, Immediate(comp->argument_count()));
  GenerateCall(comp->token_index(),
               &StubCode::CallNativeCFunctionLabel(),
               PcDescriptors::kOther);
  __ popq(RAX);
}


void FlowGraphCompiler::VisitLoadInstanceField(LoadInstanceFieldComp* comp) {
  LoadValue(comp->instance());  // -> RAX.
  __ movq(RAX, FieldAddress(RAX, comp->field().Offset()));
}


void FlowGraphCompiler::VisitStoreInstanceField(StoreInstanceFieldComp* comp) {
  VerifyValues(comp->instance(), comp->value());
  LoadValue(comp->value());
  __ movq(R10, RAX);
  LoadValue(comp->instance());  // -> RAX.
  __ StoreIntoObject(RAX, FieldAddress(RAX, comp->field().Offset()), R10);
}



void FlowGraphCompiler::VisitLoadStaticField(LoadStaticFieldComp* comp) {
  __ LoadObject(RDX, comp->field());
  __ movq(RAX, FieldAddress(RDX, Field::value_offset()));
}


void FlowGraphCompiler::VisitStoreStaticField(StoreStaticFieldComp* comp) {
  LoadValue(comp->value());
  __ LoadObject(RDX, comp->field());
  __ StoreIntoObject(RDX, FieldAddress(RDX, Field::value_offset()), RAX);
}


void FlowGraphCompiler::VisitStoreIndexed(StoreIndexedComp* comp) {
  // Call operator []= but preserve the third argument value under the
  // arguments as the result of the computation.
  const String& function_name =
      String::ZoneHandle(String::NewSymbol(Token::Str(Token::kASSIGN_INDEX)));

  // Insert a copy of the third (last) argument under the arguments.
  __ popq(RAX);  // Value.
  __ popq(RBX);  // Index.
  __ popq(RCX);  // Receiver.
  __ pushq(RAX);
  __ pushq(RCX);
  __ pushq(RBX);
  __ pushq(RAX);
  EmitInstanceCall(comp->node_id(), comp->token_index(), function_name, 3,
                   Array::ZoneHandle(), 1);
  __ popq(RAX);
}


void FlowGraphCompiler::VisitInstanceSetter(InstanceSetterComp* comp) {
  // Preserve the second argument under the arguments as the result of the
  // computation, then call the getter.
  const String& function_name =
      String::ZoneHandle(Field::SetterSymbol(comp->field_name()));

  // Insert a copy of the second (last) argument under the arguments.
  __ popq(RAX);  // Value.
  __ popq(RBX);  // Reciever.
  __ pushq(RAX);
  __ pushq(RBX);
  __ pushq(RAX);
  EmitInstanceCall(comp->node_id(), comp->token_index(), function_name, 2,
                   Array::ZoneHandle(), 1);
  __ popq(RAX);
}


void FlowGraphCompiler::VisitBooleanNegate(BooleanNegateComp* comp) {
  const Bool& bool_true = Bool::ZoneHandle(Bool::True());
  const Bool& bool_false = Bool::ZoneHandle(Bool::False());
  Label done;
  LoadValue(comp->value());
  __ movq(RDX, RAX);
  __ LoadObject(RAX, bool_true);
  __ cmpq(RAX, RDX);
  __ j(NOT_EQUAL, &done, Assembler::kNearJump);
  __ LoadObject(RAX, bool_false);
  __ Bind(&done);
}


void FlowGraphCompiler::VisitInstanceOf(InstanceOfComp* comp) {
  Bailout("InstanceOf");
}


void FlowGraphCompiler::VisitBlocks(
    const GrowableArray<BlockEntryInstr*>& blocks) {
  for (intptr_t i = blocks.length() - 1; i >= 0; --i) {
    // Compile the block entry.
    current_block_ = blocks[i];
    Instruction* instr = current_block()->Accept(this);
    // Compile all successors until an exit, branch, or a block entry.
    while ((instr != NULL) && !instr->IsBlockEntry()) {
      instr = instr->Accept(this);
    }

    BlockEntryInstr* successor =
        (instr == NULL) ? NULL : instr->AsBlockEntry();
    if (successor != NULL) {
      // Block ended with a "goto".  We can fall through if it is the
      // next block in the list.  Otherwise, we need a jump.
      if (i == 0 || (blocks[i - 1] != successor)) {
        __ jmp(&block_info_[successor->block_number()]->label);
      }
    }
  }
}


void FlowGraphCompiler::VisitJoinEntry(JoinEntryInstr* instr) {
  __ Bind(&block_info_[instr->block_number()]->label);
}


void FlowGraphCompiler::VisitTargetEntry(TargetEntryInstr* instr) {
  __ Bind(&block_info_[instr->block_number()]->label);
}


void FlowGraphCompiler::VisitPickTemp(PickTempInstr* instr) {
  // Semantics is to copy a stack-allocated temporary to the top of stack.
  // Destination index d is assumed the new top of stack after the
  // operation, so d-1 is the current top of stack and so d-s-1 is the
  // offset to source index s.
  intptr_t offset = instr->destination() - instr->source() - 1;
  ASSERT(offset >= 0);
  __ pushq(Address(RSP, offset * kWordSize));
}


void FlowGraphCompiler::VisitTuckTemp(TuckTempInstr* instr) {
  // Semantics is to assign to a stack-allocated temporary a copy of the top
  // of stack.  Source index s is assumed the top of stack, s-d is the
  // offset to destination index d.
  intptr_t offset = instr->source() - instr->destination();
  ASSERT(offset >= 0);
  __ movq(RAX, Address(RSP, 0));
  __ movq(Address(RSP, offset * kWordSize), RAX);
}


void FlowGraphCompiler::VisitDo(DoInstr* instr) {
  instr->computation()->Accept(this);
}


void FlowGraphCompiler::VisitBind(BindInstr* instr) {
  instr->computation()->Accept(this);
  __ pushq(RAX);
}


void FlowGraphCompiler::VisitReturn(ReturnInstr* instr) {
  LoadValue(instr->value());

#ifdef DEBUG
  // Check that the entry stack size matches the exit stack size.
  __ movq(R10, RBP);
  __ subq(R10, RSP);
  __ cmpq(R10, Immediate(stack_local_count() * kWordSize));
  Label stack_ok;
  __ j(EQUAL, &stack_ok, Assembler::kNearJump);
  __ Stop("Exit stack size does not match the entry stack size.");
  __ Bind(&stack_ok);
#endif  // DEBUG.

  if (FLAG_trace_functions) {
    __ pushq(RAX);  // Preserve result.
    const Function& function =
        Function::ZoneHandle(parsed_function_.function().raw());
    __ LoadObject(RBX, function);
    __ pushq(RBX);
    GenerateCallRuntime(AstNode::kNoId,
                        0,
                        kTraceFunctionExitRuntimeEntry);
    __ popq(RAX);  // Remove argument.
    __ popq(RAX);  // Restore result.
  }
  __ LeaveFrame();
  __ ret();

  // Generate 8 bytes of NOPs so that the debugger can patch the
  // return pattern with a call to the debug stub.
  __ nop(1);
  __ nop(1);
  __ nop(1);
  __ nop(1);
  __ nop(1);
  __ nop(1);
  __ nop(1);
  __ nop(1);
  AddCurrentDescriptor(PcDescriptors::kReturn,
                       AstNode::kNoId,
                       instr->token_index());
}


void FlowGraphCompiler::VisitBranch(BranchInstr* instr) {
  // Determine if the true branch is fall through (!negated) or the false
  // branch is.  They cannot both be backwards branches.
  intptr_t index = blocks_->length() - current_block()->block_number() - 1;
  ASSERT(index > 0);

  bool negated = ((*blocks_)[index - 1] == instr->false_successor());
  ASSERT(!negated == ((*blocks_)[index - 1] == instr->true_successor()));

  LoadValue(instr->value());
  __ LoadObject(RDX, Bool::ZoneHandle(Bool::True()));
  __ cmpq(RAX, RDX);
  if (negated) {
    __ j(EQUAL, &block_info_[instr->true_successor()->block_number()]->label);
  } else {
    __ j(NOT_EQUAL,
         &block_info_[instr->false_successor()->block_number()]->label);
  }
}


void FlowGraphCompiler::CompileGraph() {
  const Function& function = parsed_function_.function();
  if ((function.num_optional_parameters() != 0)) {
    Bailout("function has optional parameters");
  }
  LocalScope* scope = parsed_function_.node_sequence()->scope();
  LocalScope* context_owner = NULL;
  const int parameter_count = function.num_fixed_parameters();
  const int first_parameter_index = 1 + parameter_count;
  const int first_local_index = -1;
  int first_free_frame_index =
      scope->AllocateVariables(first_parameter_index,
                               parameter_count,
                               first_local_index,
                               scope,
                               &context_owner);
  set_stack_local_count(first_local_index - first_free_frame_index);

  // Specialized version of entry code from CodeGenerator::GenerateEntryCode.
  __ EnterFrame(stack_local_count() * kWordSize);
#ifdef DEBUG
  const bool check_arguments = true;
#else
  const bool check_arguments = function.IsClosureFunction();
#endif
  if (check_arguments) {
    // Check that num_fixed <= argc <= num_params.
    Label argc_in_range;
    // Total number of args is the first Smi in args descriptor array (R10).
    __ movq(RAX, FieldAddress(R10, Array::data_offset()));
    __ cmpq(RAX, Immediate(Smi::RawValue(parameter_count)));
    __ j(EQUAL, &argc_in_range, Assembler::kNearJump);
    if (function.IsClosureFunction()) {
      GenerateCallRuntime(AstNode::kNoId,
                          function.token_index(),
                          kClosureArgumentMismatchRuntimeEntry);
    } else {
      __ Stop("Wrong number of arguments");
    }
    __ Bind(&argc_in_range);
  }

  // Initialize locals to null.
  if (stack_local_count() > 0) {
    __ movq(RAX, Immediate(reinterpret_cast<intptr_t>(Object::null())));
    for (int i = 0; i < stack_local_count(); ++i) {
      // Subtract index i (locals lie at lower addresses than RBP).
      __ movq(Address(RBP, (first_local_index - i) * kWordSize), RAX);
    }
  }

  // Generate stack overflow check.
  __ movq(TMP, Immediate(Isolate::Current()->stack_limit_address()));
  __ cmpq(RSP, Address(TMP, 0));
  Label no_stack_overflow;
  __ j(ABOVE, &no_stack_overflow, Assembler::kNearJump);
  GenerateCallRuntime(AstNode::kNoId,
                      function.token_index(),
                      kStackOverflowRuntimeEntry);
  __ Bind(&no_stack_overflow);

  if (FLAG_print_scopes) {
    // Print the function scope (again) after generating the prologue in order
    // to see annotations such as allocation indices of locals.
    if (FLAG_print_ast) {
      // Second printing.
      OS::Print("Annotated ");
    }
    AstPrinter::PrintFunctionScope(parsed_function_);
  }

  VisitBlocks(*blocks_);

  __ int3();
  // Emit function patching code. This will be swapped with the first 13 bytes
  // at entry point.
  pc_descriptors_list_->AddDescriptor(PcDescriptors::kPatchCode,
                                      assembler_->CodeSize(),
                                      AstNode::kNoId,
                                      0,
                                      -1);
  __ jmp(&StubCode::FixCallersTargetLabel());
}


// Infrastructure copied from class CodeGenerator.
void FlowGraphCompiler::GenerateCall(intptr_t token_index,
                                     const ExternalLabel* label,
                                     PcDescriptors::Kind kind) {
  __ call(label);
  AddCurrentDescriptor(kind, AstNode::kNoId, token_index);
}


void FlowGraphCompiler::GenerateCallRuntime(intptr_t node_id,
                                            intptr_t token_index,
                                            const RuntimeEntry& entry) {
  __ CallRuntimeFromDart(entry);
  AddCurrentDescriptor(PcDescriptors::kOther, node_id, token_index);
}


// Uses current pc position and try-index.
void FlowGraphCompiler::AddCurrentDescriptor(PcDescriptors::Kind kind,
                                             intptr_t node_id,
                                             intptr_t token_index) {
  pc_descriptors_list_->AddDescriptor(kind,
                                      assembler_->CodeSize(),
                                      node_id,
                                      token_index,
                                      CatchClauseNode::kInvalidTryIndex);
}


void FlowGraphCompiler::FinalizePcDescriptors(const Code& code) {
  ASSERT(pc_descriptors_list_ != NULL);
  const PcDescriptors& descriptors = PcDescriptors::Handle(
      pc_descriptors_list_->FinalizePcDescriptors(code.EntryPoint()));
  descriptors.Verify(parsed_function_.function().is_optimizable());
  code.set_pc_descriptors(descriptors);
}


void FlowGraphCompiler::FinalizeVarDescriptors(const Code& code) {
  const LocalVarDescriptors& var_descs = LocalVarDescriptors::Handle(
          parsed_function_.node_sequence()->scope()->GetVarDescriptors());
  code.set_var_descriptors(var_descs);
}


void FlowGraphCompiler::FinalizeExceptionHandlers(const Code& code) {
  // We don't compile exception handlers yet.
  code.set_exception_handlers(
      ExceptionHandlers::Handle(ExceptionHandlers::New(0)));
}


}  // namespace dart

#endif  // defined TARGET_ARCH_X64
