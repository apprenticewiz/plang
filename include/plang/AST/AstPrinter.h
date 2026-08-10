#pragma once

#include "plang/AST/Ast.h"

#include <ostream>

namespace plang {

/// Prints the AST rooted at Program to Os as a Lisp-style S-expression.
/// Declarations and statements are indented; expressions and types are inline.
void printAst(const ProgramNode& Program, std::ostream& Os);

} // namespace plang
