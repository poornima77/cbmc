/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution of ANSI-C

#include "goto_symex.h"

#include <util/arith_tools.h>
#include <util/byte_operators.h>
#include <util/c_types.h>
#include <util/exception_utils.h>
#include <util/invariant.h>
#include <util/prefix.h>
#include <util/range.h>

static void locality(
  const irep_idt &function_identifier,
  goto_symext::statet &state,
  path_storaget &path_storage,
  const goto_functionst::goto_functiont &goto_function,
  const namespacet &ns);

bool goto_symext::get_unwind_recursion(const irep_idt &, unsigned, unsigned)
{
  return false;
}

void goto_symext::parameter_assignments(
  const irep_idt &function_identifier,
  const goto_functionst::goto_functiont &goto_function,
  statet &state,
  const exprt::operandst &arguments)
{
  const code_typet &function_type=goto_function.type;

  // iterates over the arguments
  exprt::operandst::const_iterator it1=arguments.begin();

  // iterates over the types of the parameters
  for(const auto &identifier : goto_function.parameter_identifiers)
  {
    INVARIANT(
      !identifier.empty(), "function parameter must have an identifier");

    const symbolt &symbol=ns.lookup(identifier);
    symbol_exprt lhs=symbol.symbol_expr();

    // this is the type that the n-th argument should have
    const typet &parameter_type = symbol.type;

    exprt rhs;

    // if you run out of actual arguments there was a mismatch
    if(it1==arguments.end())
    {
      log.warning() << state.source.pc->source_location.as_string()
                    << ": "
                       "call to `"
                    << id2string(function_identifier)
                    << "': "
                       "not enough arguments, inserting non-deterministic value"
                    << log.eom;

      rhs = side_effect_expr_nondett(
        parameter_type, state.source.pc->source_location);
    }
    else
      rhs=*it1;

    if(rhs.is_nil())
    {
      // 'nil' argument doesn't get assigned
    }
    else
    {
      // It should be the same exact type.
      if(parameter_type != rhs.type())
      {
        const typet &rhs_type = rhs.type();

        // But we are willing to do some limited conversion.
        // This is highly dubious, obviously.
        // clang-format off
        if(
          (parameter_type.id() == ID_signedbv ||
           parameter_type.id() == ID_unsignedbv ||
           parameter_type.id() == ID_c_enum_tag ||
           parameter_type.id() == ID_bool ||
           parameter_type.id() == ID_pointer ||
           parameter_type.id() == ID_union ||
           parameter_type.id() == ID_union_tag) &&
          (rhs_type.id() == ID_signedbv ||
           rhs_type.id() == ID_unsignedbv ||
           rhs_type.id() == ID_c_bit_field ||
           rhs_type.id() == ID_c_enum_tag ||
           rhs_type.id() == ID_bool ||
           rhs_type.id() == ID_pointer ||
           rhs_type.id() == ID_union ||
           rhs_type.id() == ID_union_tag))
        {
          rhs=
            byte_extract_exprt(
              byte_extract_id(),
              rhs,
              from_integer(0, index_type()),
              parameter_type);
        }
        else
        {
          std::ostringstream error;
          error << "function call: parameter \"" << identifier
                << "\" type mismatch: got " << rhs.type().pretty()
                << ", expected " << parameter_type.pretty();
          throw unsupported_operation_exceptiont(error.str());
        }
        // clang-format on
      }

      assignment_typet assignment_type;

      // We hide if we are in a hidden function.
      if(state.call_stack().top().hidden_function)
        assignment_type =
          symex_targett::assignment_typet::HIDDEN_ACTUAL_PARAMETER;
      else
        assignment_type =
          symex_targett::assignment_typet::VISIBLE_ACTUAL_PARAMETER;

      clean_expr(lhs, state, true);
      clean_expr(rhs, state, false);

      exprt::operandst lhs_conditions;
      symex_assign_rec(
        state, lhs, nil_exprt(), rhs, lhs_conditions, assignment_type);
    }

    if(it1!=arguments.end())
      it1++;
  }

  if(function_type.has_ellipsis())
  {
    // These are va_arg arguments; their types may differ from call to call
    std::size_t va_count=0;
    const symbolt *va_sym=nullptr;
    while(!ns.lookup(
        id2string(function_identifier)+"::va_arg"+std::to_string(va_count),
        va_sym))
      ++va_count;

    for( ; it1!=arguments.end(); it1++, va_count++)
    {
      irep_idt id=
        id2string(function_identifier)+"::va_arg"+std::to_string(va_count);

      // add to symbol table
      symbolt symbol;
      symbol.name=id;
      symbol.base_name="va_arg"+std::to_string(va_count);
      symbol.mode=ID_C;
      symbol.type=it1->type();

      state.symbol_table.insert(std::move(symbol));

      symbol_exprt lhs=symbol_exprt(id, it1->type());

      symex_assign(state, code_assignt(lhs, *it1));
    }
  }
  else if(it1!=arguments.end())
  {
    // we got too many arguments, but we will just ignore them
  }
}

void goto_symext::symex_function_call(
  const get_goto_functiont &get_goto_function,
  statet &state,
  const code_function_callt &code)
{
  const exprt &function=code.function();

  // If at some point symex_function_call can support more
  // expression ids(), like ID_Dereference, please expand the
  // precondition appropriately.
  PRECONDITION(function.id() == ID_symbol);
  symex_function_call_symbol(get_goto_function, state, code);
}

void goto_symext::symex_function_call_symbol(
  const get_goto_functiont &get_goto_function,
  statet &state,
  const code_function_callt &original_code)
{
  code_function_callt code = original_code;

  if(code.lhs().is_not_nil())
    clean_expr(code.lhs(), state, true);

  clean_expr(code.function(), state, false);

  Forall_expr(it, code.arguments())
    clean_expr(*it, state, false);

  target.location(state.guard.as_expr(), state.source);

  PRECONDITION(code.function().id() == ID_symbol);

  const irep_idt &identifier=
    to_symbol_expr(code.function()).get_identifier();

  if(identifier=="CBMC_trace")
  {
    symex_trace(state, code);
  }
  else if(has_prefix(id2string(identifier), CPROVER_FKT_PREFIX))
  {
    symex_fkt(state, code);
  }
  else
    symex_function_call_code(get_goto_function, state, code);
}

void goto_symext::symex_function_call_code(
  const get_goto_functiont &get_goto_function,
  statet &state,
  const code_function_callt &call)
{
  const irep_idt &identifier=
    to_symbol_expr(call.function()).get_identifier();

  const goto_functionst::goto_functiont &goto_function =
    get_goto_function(identifier);

  state.dirty.populate_dirty_for_function(identifier, goto_function);

  auto emplace_safe_pointers_result =
    path_storage.safe_pointers.emplace(identifier, local_safe_pointerst{});
  if(emplace_safe_pointers_result.second)
    emplace_safe_pointers_result.first->second(goto_function.body);

  const bool stop_recursing = get_unwind_recursion(
    identifier,
    state.source.thread_nr,
    state.call_stack().top().loop_iterations[identifier].count);

  // see if it's too much
  if(stop_recursing)
  {
    if(symex_config.partial_loops)
    {
      // it's ok, ignore
    }
    else
    {
      if(symex_config.unwinding_assertions)
        vcc(false_exprt(), "recursion unwinding assertion", state);

      // add to state guard to prevent further assignments
      state.guard.add(false_exprt());
    }

    symex_transition(state);
    return;
  }

  // read the arguments -- before the locality renaming
  const exprt::operandst &arguments = call.arguments();
  const std::vector<renamedt<exprt, L2>> renamed_arguments =
    make_range(arguments).map(
      [&](const exprt &a) { return state.rename(a, ns); });

  // we hide the call if the caller and callee are both hidden
  const bool callee_is_hidden = ns.lookup(identifier).is_hidden();
  const bool hidden =
    state.call_stack().top().hidden_function && callee_is_hidden;

  // record the call
  target.function_call(
    state.guard.as_expr(), identifier, renamed_arguments, state.source, hidden);

  if(!goto_function.body_available())
  {
    no_body(identifier);

    // record the return
    target.function_return(
      state.guard.as_expr(), identifier, state.source, hidden);

    if(call.lhs().is_not_nil())
    {
      const auto rhs =
        side_effect_expr_nondett(call.lhs().type(), call.source_location());
      code_assignt code(call.lhs(), rhs);
      symex_assign(state, code);
    }

    symex_transition(state);
    return;
  }

  // produce a new frame
  PRECONDITION(!state.call_stack().empty());
  framet &frame = state.call_stack().new_frame(state.source);

  // preserve locality of local variables
  locality(identifier, state, path_storage, goto_function, ns);

  // assign actuals to formal parameters
  parameter_assignments(identifier, goto_function, state, arguments);

  frame.end_of_function=--goto_function.body.instructions.end();
  frame.return_value=call.lhs();
  frame.function_identifier=identifier;
  frame.hidden_function = callee_is_hidden;

  const framet &p_frame = state.call_stack().previous_frame();
  for(const auto &pair : p_frame.loop_iterations)
  {
    if(pair.second.is_recursion)
      frame.loop_iterations.insert(pair);
  }

  // increase unwinding counter
  frame.loop_iterations[identifier].is_recursion=true;
  frame.loop_iterations[identifier].count++;

  state.source.function_id = identifier;
  symex_transition(state, goto_function.body.instructions.begin(), false);
}

/// pop one call frame
static void pop_frame(goto_symext::statet &state)
{
  PRECONDITION(!state.call_stack().empty());

  {
    const framet &frame = state.call_stack().top();

    // restore program counter
    symex_transition(state, frame.calling_location.pc, false);
    state.source.function_id = frame.calling_location.function_id;

    // restore L1 renaming
    state.level1.restore_from(frame.old_level1);

    // clear function-locals from L2 renaming
    for(auto c_it = state.get_level2().current_names.begin();
        c_it != state.get_level2().current_names.end();) // no ++c_it
    {
      const irep_idt l1_o_id=c_it->second.first.get_l1_object_identifier();
      // could use iteration over local_objects as l1_o_id is prefix
      if(
        frame.local_objects.find(l1_o_id) == frame.local_objects.end() ||
        (state.threads.size() > 1 &&
         state.dirty(c_it->second.first.get_object_name())))
      {
        ++c_it;
        continue;
      }
      auto cur = c_it;
      ++c_it;
      state.drop_l1_name(cur);
    }
  }

  state.call_stack().pop();
}

/// do function call by inlining
void goto_symext::symex_end_of_function(statet &state)
{
  const bool hidden = state.call_stack().top().hidden_function;

  // first record the return
  target.function_return(
    state.guard.as_expr(), state.source.function_id, state.source, hidden);

  // then get rid of the frame
  pop_frame(state);
}

/// Preserves locality of parameters of a given function by applying L1
/// renaming to them.
static void locality(
  const irep_idt &function_identifier,
  goto_symext::statet &state,
  path_storaget &path_storage,
  const goto_functionst::goto_functiont &goto_function,
  const namespacet &ns)
{
  unsigned &frame_nr=
    state.threads[state.source.thread_nr].function_frame[function_identifier];
  frame_nr++;

  for(const auto &param : goto_function.parameter_identifiers)
  {
    (void)state.add_object(
      ns.lookup(param).symbol_expr(),
      [&path_storage, &frame_nr](const irep_idt &l0_name) {
        return path_storage.get_unique_l1_index(l0_name, frame_nr);
      },
      ns);
  }
}
