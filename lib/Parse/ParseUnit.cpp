//===- ParseUnit.cpp - Parsing of Turbo Pascal units (interface/implementation). ===//
//
// Turbo Tier 4, Cluster A item 0: PARSING ONLY.  No Sema, no CodeGen -- see
// UnitNode's own comment in AstDecl.h and ProgramNode::BareUnit's for the
// shape decisions this file implements.
//
// Grammar (confirmed against real `fpc -Mtp`; see this item's own report for
// every case that was checked):
//
//   unit-file       → 'unit' identifier ';'
//                      'interface' [uses-clause] unit-decl-part
//                      'implementation' [uses-clause] unit-decl-part
//                      [ compound-stmt ]
//                      'end' '.'
//   uses-clause     → 'uses' identifier (',' identifier)* ';'
//   unit-decl-part  → { const-section | type-section | var-section
//                       | proc-or-func-decl }
//
// A `uses` clause, when present, is the very first thing in its section --
// fpc rejects one written after a declaration (`"IMPLEMENTATION" expected
// but "USES" found"`), so this parses it before entering the declaration
// loop, not inside it.
//
// An interface section may be completely empty (interface immediately
// followed by implementation, exporting nothing); either uses clause may be
// entirely absent; and the whole unit may have no initialization code at
// all, running straight from its last implementation declaration into
// `end.` with no `begin` -- all three confirmed empirically.

#include "plang/Parse/Parser.h"
#include "ParserInternal.h"
#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"

using namespace plang;

// unit-file (see grammar above).  Current is on 'unit'.
std::unique_ptr<ProgramNode> Parser::parseUnitFile() {
    auto Unit = std::make_unique<UnitNode>();
    Unit->Loc = Current;
    expect(TokenKind::Unit);
    Unit->Name = expect(TokenKind::Identifier).Lexeme;
    expect(TokenKind::Semicolon);

    expect(TokenKind::Interface);
    if (check(TokenKind::Uses))
        Unit->InterfaceUses = parseUsesClause();
    Unit->InterfaceBlock = parseUnitDeclarations(/*HeadingsOnly=*/true);

    expect(TokenKind::Implementation);
    if (check(TokenKind::Uses))
        Unit->ImplementationUses = parseUsesClause();
    Unit->ImplementationBlock = parseUnitDeclarations(/*HeadingsOnly=*/false);

    // The single optional initialization block.  Real Turbo Pascal (and
    // fpc -Mtp) requires no empty 'begin end' placeholder when there is no
    // initialization code -- the unit runs straight into 'end.' instead.
    // parseCompoundStmt() consumes its own closing 'end' (compound-stmt →
    // 'begin' ... 'end'), which IS the unit's closing 'end' when present --
    // there is no second 'end' to expect afterwards, unlike the no-init-body
    // case just below.
    if (check(TokenKind::Begin))
        Unit->InitBody = parseCompoundStmt();
    else
        expect(TokenKind::End);
    expect(TokenKind::Dot);
    expect(TokenKind::Eof);

    // Parser::parse() keeps one return type (unique_ptr<ProgramNode>) for
    // both a program and a standalone unit; see ProgramNode::BareUnit's own
    // comment in AstDecl.h.  Name/Block here are inert placeholders -- never
    // consulted, since Frontend.cpp checks BareUnit before this ProgramNode
    // is handed to Sema or CodeGen at all.
    auto Prog  = std::make_unique<ProgramNode>();
    Prog->Loc  = Unit->Loc;
    Prog->Name = Unit->Name;
    auto Blk   = std::make_unique<BlockNode>();
    Blk->Loc   = Unit->Loc;
    Prog->Block = std::move(Blk);
    Prog->BareUnit = std::move(Unit);
    return Prog;
}

// uses-clause → 'uses' identifier (',' identifier)* ';'
//
// Every name here goes into QualifiedModules_ unconditionally -- Turbo has
// no 'qualified' keyword the way EP's own import does (QualifiedModules_'s
// other populator, ParseModule.cpp), so a Turbo `uses` clause is always
// "qualified" in that sense: `UnitName.Identifier` has to parse (fold to one
// dotted IdentExpr, see ParseExpr.cpp/ParseStmt.cpp/ParseType.cpp's own
// QualifiedModules_ checks) for EVERY unit ever named in ANY uses clause --
// a program's own, or a unit's interface/implementation uses -- because
// Cluster A item 1's own shadowing design lets a later `uses` hide an
// earlier one's same-named export, and UnitName.Identifier is how a caller
// reaches the SHADOWED one explicitly.  This is shared by every caller of
// this function, so it lives here rather than being repeated at each of the
// three call sites (program heading, interface uses, implementation uses).
std::vector<UsedUnit> Parser::parseUsesClause() {
    std::vector<UsedUnit> Units;
    expect(TokenKind::Uses);
    do {
        UsedUnit U;
        U.Loc  = Current;
        U.Name = expect(TokenKind::Identifier).Lexeme;
        QualifiedModules_.insert(toLower(U.Name));
        Units.push_back(std::move(U));
    } while (match(TokenKind::Comma));
    expect(TokenKind::Semicolon);
    return Units;
}

// unit-decl-part, in Turbo's always-free declaration order.  Modeled on
// EP's own parseModuleDeclarations (ParseModule.cpp) -- same shape, kept as
// a separate method because UnitNode is deliberately not ModuleNode.
std::unique_ptr<BlockNode> Parser::parseUnitDeclarations(bool HeadingsOnly) {
    auto Block = std::make_unique<BlockNode>();
    Block->Loc = Current;

    bool More = true;
    while (More) {
        if      (check(TokenKind::Label))     { parseLabelSection(*Block); }
        else if (check(TokenKind::Const))     { parseConstSection(*Block); }
        else if (check(TokenKind::Type))      { parseTypeSection(*Block);  }
        else if (check(TokenKind::Var))       { parseVarSection(*Block);   }
        else if (check(TokenKind::Procedure) || check(TokenKind::Function) ||
                 // Turbo Tier 5: a unit's implementation section is exactly
                 // where an object-type method's out-of-line body normally
                 // lives -- 'constructor TAnimal.Init; ...' -- see
                 // parseProcDecl's own dotted-heading handling.  A unit's
                 // own interface section can never reach this arm with
                 // Constructor/Destructor since a method heading belongs
                 // inside its object type's own 'object ... end', not the
                 // interface's top-level declaration list.
                 (Opts.turbo() && (check(TokenKind::Constructor) ||
                                   check(TokenKind::Destructor))))
            Block->Procs.push_back(
                parseProcDecl(check(TokenKind::Function), HeadingsOnly));
        else More = false;
    }
    return Block;
}
