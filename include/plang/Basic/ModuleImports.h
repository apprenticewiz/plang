#pragma once

#include "plang/Basic/StringUtil.h"

#include <map>
#include <string>
#include <string_view>

namespace plang {

/// EP §6.11: what an importing unit knows about one name it imports.
///
/// Codegen cannot work either field out from the AST.  A module-level name is
/// mangled with the module that declares it, and the reference records only
/// the identifier; and a parameterless function mentioned in an expression
/// looks exactly like a variable until you know it is a function.  Both
/// answers depend on the import clauses of the unit being emitted, so Sema
/// computes them and hands them over.
struct ImportedName {
    /// Lowercased name of the module that declares this, which is the module
    /// its symbol is mangled with — not necessarily the one it was imported
    /// from, which may have got it from somewhere else.
    std::string Module;
    /// True for a procedure or function, so a bare mention of it is a call.
    bool IsCallable{false};
    /// EP §6.11.2: the name the declaring module knows it by, which is what
    /// its mangled name is built from.  Renaming on export or on import makes
    /// this differ from the name written here.  Empty when they are the same.
    std::string LinkName;
};

/// Names one unit imports, keyed by the lowercased name as written there.
using ImportedNameMap = std::map<std::string, ImportedName>;

/// Per importing unit: a lowercased module name, or "" for the program.
using ImportOwnerTable = std::map<std::string, ImportedNameMap>;

/// EP §6.11: modules the implementation provides rather than the program.
/// They have no declaration to find and nothing to initialise, so importing
/// one is not a reason to look for a .pmi or an __plang_init_ symbol.
[[nodiscard]] inline bool isBuiltinModule(std::string_view Name) {
    return eqCI(Name, "standardinput") || eqCI(Name, "standardoutput");
}

} // namespace plang
