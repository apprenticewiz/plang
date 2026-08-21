// CGLinkage.h — EP §6.11 name resolution and mangling.
//
// Every symbol a compiled program defines for something the *source* named — a
// procedure, a function, a variable — is built from one of these prefixes.  The
// runtime's own ~150 entry points are the other half of the same link, and they
// are spelled `plang_*`.
//
// So user code may not be spelled `plang_*` too, which it was until 0.1.3: a
// procedure the program called `close`, `round`, `page` or `halt` — thirty-three
// names collided, twenty-four of them required identifiers ISO §6.2.2.10
// entitles a program to redeclare — was emitted as `plang_close`, the same
// symbol the runtime defines.  Whether that was caught depended on whether the
// runtime translation unit holding the twin happened to be pulled out of the
// archive for some other reason, so it was a duplicate-symbol error in some
// programs and silence in others.
//
// `pas_` and `pasg_` cannot collide with the runtime whatever the program
// declares, since nothing in the runtime begins with either.
//
// An enclosing scope — a module, or a procedure a procedure is nested in — is
// joined on with `$`.  Extended Pascal §6.1.3 allows an underscore inside an
// identifier, so the `__` that used to join them was itself something a name
// could contain, and a module `a` exporting `b` and a top-level `a__b` both
// wanted the same symbol.  `$` is not in the Pascal alphabet, so a mangled name
// now separates into its parts exactly one way.  It is accepted unquoted in an
// LLVM identifier, and in an ELF and a Mach-O symbol.
#pragma once

#include <string>

#include "llvm/IR/Module.h"

#include "plang/Basic/ModuleImports.h"

/// Prefix for a procedure or function the source declares.
inline constexpr const char* PlangProcPrefix   = "pas_";
/// Prefix for a variable the source declares at file or module scope.
inline constexpr const char* PlangGlobalPrefix = "pasg_";
/// Joins an enclosing scope to what it declares.  Must be something no Pascal
/// identifier can contain, or a mangled name is ambiguous; see above.
inline constexpr const char* PlangScopeSep     = "$";

class CGLinkage {
public:
    /// NamePrefix/GlobalPrefix/CurrentUnit bind to Codegen::Impl's own
    /// fields of those names -- they're mutated directly from ~25 call
    /// sites across Codegen::emit's module loop and emitFunctionDef's own
    /// save/restore, so this reads whatever value is currently in force
    /// rather than owning a private copy. ImportOwners is captured by
    /// value: it's written exactly once, by Codegen::setImportOwners,
    /// always before Impl::init() (and therefore this constructor) runs.
    CGLinkage(llvm::Module& Mod, std::string& NamePrefix, std::string& GlobalPrefix,
              std::string& CurrentUnit, const plang::ImportOwnerTable* ImportOwners)
        : Mod(Mod), NamePrefix(NamePrefix), GlobalPrefix(GlobalPrefix),
          CurrentUnit(CurrentUnit), ImportOwners(ImportOwners) {}

    /// EP §6.11: a module is an outer naming scope, so what it declares is
    /// mangled with its name the way a nested procedure is mangled with its
    /// enclosing one.  Without this, two modules each exporting `f` both want
    /// the symbol `plang_f`, and the second definition is renamed to
    /// `plang_f.1` and never called.
    static std::string moduleScope(const std::string& moduleName);
    /// Drops the module qualifier from an EP §6.11.2 qualified name, leaving
    /// the identifier.  The module it names is recovered separately, by
    /// importOwner, because it is part of the mangled name.
    static std::string stripQualifier(const std::string& name);

    /// What is known about \p name as an import of the unit being emitted, or
    /// null if this unit does not import it.
    const plang::ImportedName* importedName(const std::string& name) const;
    /// The module that declares \p name as this unit sees it, or "" when the
    /// name is not imported.  A qualified name answers for itself: the parser
    /// folds `M.f` into one identifier, and M is the module.
    std::string importOwner(const std::string& name) const;
    /// The name \p name is mangled under in the module that declares it.  EP
    /// §6.11.2 renaming lets a unit call an imported procedure something else
    /// entirely; the object file still knows only the original.
    std::string importLinkName(const std::string& name) const;
    /// True when \p name is an imported procedure or function, so a bare
    /// mention of it is a call even though nothing of that name has been
    /// emitted here — its module was compiled separately.
    bool isImportedCallable(const std::string& name) const;

    /// Resolve the LLVM mangled name for a Pascal procedure/function call.
    /// Walks outward through the nesting hierarchy so that a call to 'inner'
    /// from inside 'outer' finds 'plang_outer__inner', while a call to a
    /// top-level 'helper' from inside 'outer' falls back to 'plang_helper'.
    std::string findMangledProc(const std::string& qualifiedName) const;
    /// The symbol naming the global variable \p name denotes, mangled with the
    /// module that declares it.
    std::string mangledGlobal(const std::string& qualifiedName) const;

private:
    llvm::Module& Mod;
    std::string& NamePrefix;
    std::string& GlobalPrefix;
    std::string& CurrentUnit;
    const plang::ImportOwnerTable* ImportOwners;
};
