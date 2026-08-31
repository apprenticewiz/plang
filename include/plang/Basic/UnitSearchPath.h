//===----------------------------------------------------------------------===//
// Where a Turbo `uses` clause finds the shipped RTL, once a program has been
// installed and is invoked with no search-path flags at all.
//===----------------------------------------------------------------------===//
//
// Tier 4, Cluster B item 4.  Deliberately its own tiny header/TU, mirroring
// MessageCatalog.h's catalogSearchPaths(): the two problems -- "where does an
// installed plang find its shipped .po catalogs" and "where does it find its
// shipped .tui/.pas units" -- are unrelated in content but identical in shape,
// so the resolution logic is the same three tiers, just naming a different
// directory and a different environment variable.
//===----------------------------------------------------------------------===//
#ifndef PLANG_BASIC_UNITSEARCHPATH_H
#define PLANG_BASIC_UNITSEARCHPATH_H

#include <string>
#include <vector>

namespace plang {

/// Directories to look in for a Turbo unit's shipped RTL file, most specific
/// first: PLANG_UNIT_DIR if it is set, then <the directory holding this
/// binary>/../lib/plang/units, then the build-tree path compiled in as
/// PLANG_UNIT_DIR (the macro, not the environment variable of the same name --
/// see the source file).
///
/// This is a THIRD, separate list from LangOptions::ModuleSearchPaths (EP's
/// own -I/.pmi search path) and LangOptions::IncludeSearchPaths (Turbo's own
/// {$I} search path) -- see IncludeSearchPaths's own comment for why -I
/// already means something else and cannot be reused here either.  The
/// caller (Frontend.cpp) appends these paths onto LangOptions::UnitSearchPaths,
/// which Sema::loadUnitInterfaceExports consults last, after ModuleSearchPaths
/// and ".": a user's own -I directories and the current directory are meant to
/// shadow the shipped RTL, not the other way around.
std::vector<std::string> unitSearchPaths(const std::string& ExeDir);

} // namespace plang

#endif // PLANG_BASIC_UNITSEARCHPATH_H
