#pragma once

#include "spp/base/expected.h"
#include "spp/query/query_ast.h"

#include <string_view>

namespace spp::query {

// Minimal grammar:
//   query   := or
//   or      := and ( "OR" and )*
//   and     := atom ( ("AND" | <implicit space>) atom )*
//   atom    := field ":" term | term | "(" or ")"
//   field   := identifier
//   term    := identifier | quoted_string
//
// `default_field` is the field used when no `field:` qualifier is present.
Expected<QueryAst> Parse(std::string_view input, std::string_view default_field);

}  // namespace spp::query
