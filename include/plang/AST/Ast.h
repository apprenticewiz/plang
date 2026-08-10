#pragma once

// Umbrella header: includes all AST node definitions.
// Most consumers only need this one include.
//
// For fine-grained dependencies, include the specific sub-headers:
//   AstBase.h  — NodeKind enum and abstract base nodes (Node, ExprNode, StmtNode, TypeNode)
//   AstExpr.h  — concrete expression nodes
//   AstType.h  — concrete type nodes, FieldDecl, VariantCase, VariantPart
//   AstStmt.h  — concrete statement nodes, CaseArm
//   AstDecl.h  — ConstDef, TypeDef, VarGroup, ParamGroup, ProcDecl, BlockNode, ProgramNode

#include "plang/AST/AstBase.h"
#include "plang/AST/AstExpr.h"
#include "plang/AST/AstType.h"
#include "plang/AST/AstStmt.h"
#include "plang/AST/AstDecl.h"
