//===- ParseModule.cpp - Parsing of Extended Pascal modules, exports and imports (ISO 10206 §6.12). ===//

#include "plang/Parse/Parser.h"
#include "ParserInternal.h"
#include "plang/AST/Ast.h"
#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/StringUtil.h"
#include "plang/Basic/Token.h"
#include "llvm/Support/Casting.h"

#include <cctype>
#include <charconv>
#include <format>
#include <string>

using namespace plang;

// ---------------------------------------------------------------------------
// EP §6.11: multi-unit file parsing
// ---------------------------------------------------------------------------

// Collect module definitions until we see 'program', then parse the program.
// If the file contains only modules and no 'program' keyword (module-only file),
// a synthetic ProgramNode with an empty block is returned so that the rest of
// the pipeline (Sema, CodeGen) can process the module bodies normally.
std::unique_ptr<ProgramNode> Parser::parseMultiUnitFile() {
    std::vector<std::unique_ptr<ModuleNode>> Mods;
    while (check(TokenKind::Module)) parseModuleDeclaration(Mods);

    // Module-only file: no 'program' keyword before EOF.
    if (!Mods.empty() && check(TokenKind::Eof)) {
        auto Prog  = std::make_unique<ProgramNode>();
        Prog->Loc  = Current;
        Prog->Name = "__module_only__";
        // Synthesize an empty block (no compound body).
        auto Blk  = std::make_unique<BlockNode>();
        Blk->Loc  = Current;
        Prog->Block = std::move(Blk);
        for (auto& M : Mods) {
            Prog->Modules.push_back(M.get());
            Prog->OwnedModules.push_back(std::move(M));
        }
        return Prog;
    }

    auto Prog = parseProgram();
    if (!Prog) return nullptr;
    for (auto& M : Mods) {
        Prog->Modules.push_back(M.get());
        Prog->OwnedModules.push_back(std::move(M));
    }
    return Prog;
}

// EP §6.11.1:
//   module-declaration = module-heading [ ';' module-block ]
//                      | module-identification ';' module-block
//   module-heading = 'module' identifier [ 'interface' ] [ '(' params ')' ] ';'
//                    interface-specification-part
//                    { declaration-part } 'end'
//   module-identification = 'module' identifier 'implementation'
//   module-block = import-part { declaration-part }
//                  [ 'to' 'begin' 'do' stmt ';' ] [ 'to' 'end' 'do' stmt ';' ]
//                  'end'
//
// A heading and the block that follows it are two units of the same module,
// so they are read into two nodes: the same pair a program gets when the two
// are written apart, which is what everything downstream already handles.
void Parser::parseModuleDeclaration(
        std::vector<std::unique_ptr<ModuleNode>>& Out) {
    auto Node = std::make_unique<ModuleNode>();
    Node->Loc  = Current;
    expect(TokenKind::Module);
    Node->Name = expect(TokenKind::Identifier).Lexeme;

    // The directive comes before the parameter list, though plang has long
    // read 'interface' after it too, so both places are looked in.
    auto atImplementation = [&] {
        // 'implementation' is a directive rather than a word of the language,
        // so it arrives as an identifier and is known by its spelling.
        return check(TokenKind::Identifier)
            && toLower(Current.Lexeme) == "implementation";
    };
    if (check(TokenKind::Interface))  { Node->IsInterface = true;      advance(); }
    else if (atImplementation())      { Node->IsImplementation = true; advance(); }

    if (match(TokenKind::LeftParen)) {
        Node->Params.push_back(expect(TokenKind::Identifier).Lexeme);
        while (match(TokenKind::Comma))
            Node->Params.push_back(expect(TokenKind::Identifier).Lexeme);
        expect(TokenKind::RightParen);
    }

    if (!Node->IsInterface && !Node->IsImplementation && check(TokenKind::Interface)) {
        Node->IsInterface = true;
        advance();
    }
    expect(TokenKind::Semicolon);

    // EP §6.11.2: an interface is an export part followed by the declarations
    // of what it names.  A procedure or function appears as its heading alone,
    // there being no body to give until the implementation module.
    if (Node->IsInterface) {
        Node->Imports = parseImportClauses();
        if (check(TokenKind::Export)) {
            advance(); // consume 'export'
            Node->Exports = parseExportSection();
        }
        Node->Body = parseModuleDeclarations(/*HeadingsOnly=*/true);
        expect(TokenKind::End);
        // A heading ends the module only when a program or another module
        // follows it; a semicolon instead says its block comes next, here.
        if (match(TokenKind::Semicolon)) {
            auto* Iface = Node.get();
            Out.push_back(std::move(Node));
            auto Impl = std::make_unique<ModuleNode>();
            Impl->Loc              = Current;
            Impl->Name             = Iface->Name;
            Impl->IsImplementation = true;
            parseModuleBlock(*Impl);
            Out.push_back(std::move(Impl));
            return;
        }
        expect(TokenKind::Dot);
        Out.push_back(std::move(Node));
        return;
    }

    parseModuleBlock(*Node);
    Out.push_back(std::move(Node));
}

// The module-block of EP §6.11.1, from its import-part to the '.' that ends
// the declaration.
void Parser::parseModuleBlock(ModuleNode& Node) {
    Node.Imports = parseImportClauses();
    Node.Body    = parseModuleDeclarations(/*HeadingsOnly=*/false);

    // Optional 'to begin do' and/or 'to end do' (EP §6.11.1).
    // Both may appear in any order; each is: 'to' ('begin'|'end') 'do' stmt ';'
    while (check(TokenKind::To)) {
        advance(); // consume 'to'
        if (check(TokenKind::Begin)) {
            advance(); // consume 'begin'
            expect(TokenKind::Do);
            Node.InitStmt = parseStatement();
            match(TokenKind::Semicolon);
        } else if (check(TokenKind::End)) {
            advance(); // consume 'end' (part of 'to end do', NOT the module-closing end)
            expect(TokenKind::Do);
            Node.FinalStmt = parseStatement();
            match(TokenKind::Semicolon);
        } else {
            emitError(Current.toLoc(),
                      "'to' in module body must be followed by 'begin' or 'end'");
            break; // avoid infinite loop on unexpected token
        }
    }

    expect(TokenKind::End);
    expect(TokenKind::Dot);
}

// The declaration sections of a module, in any order (EP §6.2.1), gathered
// into a block with no statement part.  In an interface a procedure or
// function is written as its heading alone.
std::unique_ptr<BlockNode> Parser::parseModuleDeclarations(bool HeadingsOnly) {
    auto Block = std::make_unique<BlockNode>();
    Block->Loc = Current;

    bool More = true;
    while (More) {
        if      (check(TokenKind::Label))     { parseLabelSection(*Block); }
        else if (check(TokenKind::Const))     { parseConstSection(*Block); }
        else if (check(TokenKind::Type))      { parseTypeSection(*Block);  }
        else if (check(TokenKind::Var))       { parseVarSection(*Block);   }
        else if (check(TokenKind::Procedure) || check(TokenKind::Function))
            Block->Procs.push_back(
                parseProcDecl(check(TokenKind::Function), HeadingsOnly));
        else More = false;
    }
    return Block;
}

// EP §6.11.2:
//   interface-specification-part → 'export' export-part ';' { export-part ';' }
//   export-part   → identifier '=' '(' export-clause { ',' export-clause } ')'
//   export-clause → ['protected'] name ['=>' identifier] | first '..' last
//
// The export-part is named, because a module may offer more than one interface
// and an importer names the one it wants.  plang has a single interface per
// module, so the name is checked against the module's and otherwise unused.
//
// Also accepted is the older plang spelling, in which the export section is a
// list of the declarations themselves rather than of their names.
std::vector<ExportItem> Parser::parseExportSection() {
    std::vector<ExportItem> Items;

    // Anything that is not an identifier begins a declaration rather than an
    // export-part: that is the older spelling, where the section is the
    // declarations themselves and everything in it is exported.  An empty
    // list is what tells Sema so.
    if (!check(TokenKind::Identifier)) return Items;

    const Token       NameTok = Current;
    const std::string Name    = Current.Lexeme;
    advance();

    if (match(TokenKind::Equal) && match(TokenKind::LeftParen)) {
        parseExportList(Items);
        expect(TokenKind::RightParen);
        expect(TokenKind::Semicolon);
        // Further export-parts, each naming an interface of its own.  plang
        // gives a module one interface, so they all describe the same one.
        // One 'export' introduces them all; repeating it is also read, since
        // that is how the parts would be written if they were sections.
        while (check(TokenKind::Identifier) || check(TokenKind::Export)) {
            match(TokenKind::Export);
            expect(TokenKind::Identifier);
            expect(TokenKind::Equal);
            expect(TokenKind::LeftParen);
            parseExportList(Items);
            expect(TokenKind::RightParen);
            expect(TokenKind::Semicolon);
        }
        return Items;
    }

    // Older plang spelling: a list of the names being exported, the first of
    // which is already in hand.  The '=' of a rename has been consumed by the
    // test above, so what remains of one is the '>'.
    ExportItem First;
    First.Loc  = NameTok;
    First.Name = Name;
    if (match(TokenKind::GreaterThan))
        First.Alias = expect(TokenKind::Identifier).Lexeme;
    Items.push_back(std::move(First));
    while (match(TokenKind::Comma)) {
        ExportItem Item;
        Item.Loc  = Current;
        Item.Name = expect(TokenKind::Identifier).Lexeme;
        if (check(TokenKind::Equal)) parseExportRename(Item);
        Items.push_back(std::move(Item));
    }
    match(TokenKind::Semicolon);
    return Items;
}

// export-clause { ',' export-clause }, appended to Items.
void Parser::parseExportList(std::vector<ExportItem>& Items) {
    if (check(TokenKind::RightParen)) return;
    do {
        ExportItem Item;
        // EP §6.11.2: 'protected' before a variable lets importers read it
        // without being able to change it.
        if (check(TokenKind::Protected)) { Item.Protected = true; advance(); }
        Item.Loc  = Current;
        Item.Name = expect(TokenKind::Identifier).Lexeme;
        // An export-range names the first and last of a run of constants.
        if (match(TokenKind::DotDot))
            Item.RangeEnd = expect(TokenKind::Identifier).Lexeme;
        else if (check(TokenKind::Equal))
            parseExportRename(Item);
        Items.push_back(std::move(Item));
    } while (match(TokenKind::Comma));
}

// '=>' identifier, with Current on the '='.  The scanner has no '=>' token,
// so the two halves are matched here and an '=' that is not part of one is
// left alone for the caller to make sense of.
void Parser::parseExportRename(ExportItem& Item) {
    advance(); // consume '='
    expect(TokenKind::GreaterThan);
    Item.Alias = expect(TokenKind::Identifier).Lexeme;
}

// EP §6.11.3:
//   import-part          → { 'import' import-specification ';' }
//   import-specification → interface-identifier ['qualified'] [import-qualifier]
//   import-qualifier     → ['only'] '(' import-clause { ',' import-clause } ')'
//   import-clause        → identifier ['=>' identifier]
//
// 'qualified' and the name list may be written in either order; the standard
// fixes one, and neither reading is ambiguous.  A list without parentheses is
// the older plang spelling of 'only'.
std::vector<ImportClause> Parser::parseImportClauses() {
    std::vector<ImportClause> Clauses;
    while (check(TokenKind::Import)) {
        ImportClause Clause;
        Clause.Loc = Current;
        advance(); // consume 'import'
        Clause.ModuleName = expect(TokenKind::Identifier).Lexeme;

        bool More = true;
        while (More) {
            if (check(TokenKind::Qualified)) {
                advance();
                Clause.Qualified = true;
                // Remembered so an expression can tell M.f (a qualified
                // reference) from r.f (a field of a record variable named r).
                QualifiedModules_.insert(toLower(Clause.ModuleName));
            } else if (check(TokenKind::Only)) {
                advance();
                Clause.Selective = true;
                if (match(TokenKind::LeftParen)) {
                    parseImportList(Clause);
                    expect(TokenKind::RightParen);
                } else {
                    parseImportList(Clause); // older spelling: no parentheses
                }
            } else if (match(TokenKind::LeftParen)) {
                parseImportList(Clause);
                expect(TokenKind::RightParen);
            } else {
                More = false;
            }
        }

        expect(TokenKind::Semicolon);
        Clauses.push_back(std::move(Clause));
    }
    return Clauses;
}

// import-clause { ',' import-clause }, recorded in Clause.
void Parser::parseImportList(ImportClause& Clause) {
    if (check(TokenKind::RightParen)) return;
    do {
        std::string Name = expect(TokenKind::Identifier).Lexeme;
        if (check(TokenKind::Equal)) {
            advance(); // consume '='
            expect(TokenKind::GreaterThan);
            Clause.Renames.emplace_back(Name,
                                        expect(TokenKind::Identifier).Lexeme);
        }
        Clause.Names.push_back(std::move(Name));
    } while (match(TokenKind::Comma));
}
