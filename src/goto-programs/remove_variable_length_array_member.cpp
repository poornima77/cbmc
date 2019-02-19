/*******************************************************************\

Module: Remove gcc's 'variable-length array members'

Author: Daniel Kroening

Date:   February 2019

\*******************************************************************/

/// \file
/// Remove gcc's 'variable-length array members'
/// https://gcc.gnu.org/onlinedocs/gcc/Variable-Length.html

#include "remove_variable_length_array_member.h"

#include "goto_model.h"

#include <util/c_types.h>
#include <util/pointer_offset_size.h>

bool has_variable_length_array_member(const typet &type, const namespacet &ns)
{
  if(type.id() == ID_struct_tag)
  {
    const auto &struct_type = ns.follow_tag(to_struct_tag_type(type));

    for(auto &c : struct_type.components())
    {
      const auto &c_type = c.type();
      if(c_type.id() == ID_struct_tag)
      {
        if(has_variable_length_array_member(c.type(), ns))
          return true;
      }
      else if(c_type.id() == ID_array)
      {
        if(!to_array_type(c_type).size().is_constant())
          return true;
      }
    }

    return false;
  }
  else
    return false;
}

bool has_variable_length_array_member(const exprt &expr, const namespacet &ns)
{
}

static array_typet replacement_type(const typet &type, const namespacet &ns)
{
  const exprt size = size_of_expr(type, ns);
  CHECK_RETURN(size.is_not_nil());
  return array_typet(char_type(), size);
}

void remove_variable_length_array_member(exprt &expr, const namespacet &ns)
{
}

void remove_variable_length_array_member(typet &type, const namespacet &ns)
{
}

/// removes gcc's 'variable-length array members'
void remove_variable_length_array_member(goto_modelt &goto_model)
{
  const namespacet ns(goto_model.symbol_table);

  for(auto &f : goto_model.goto_functions.function_map)
    for(auto &i : f.second.body.instructions)
    {
      i.transform([&ns](exprt e) -> optionalt<exprt> {
        if(has_variable_length_array_member(e, ns))
        {
          remove_variable_length_array_member(e, ns);
          return e;
        }
        else
          return {};
      });
    }

  for(const auto &s : goto_model.symbol_table.symbols)
    if(has_variable_length_array_member(s.second.type, ns))
      remove_variable_length_array_member(
        goto_model.symbol_table.get_writeable(s.first)->type, ns);
}
