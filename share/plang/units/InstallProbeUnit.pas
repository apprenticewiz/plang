// Turbo Tier 4, Cluster B item 4: install-layout plumbing proof, not real RTL
// content.
//
// This is the ONE file this item ships into <prefix>/lib/plang/units/ (see
// the top-level CMakeLists.txt's own install(DIRECTORY share/plang/units/ ...)
// rule).  Its entire job is to exist somewhere an installed `plang` can find
// it with no -I and no PLANG_UNIT_DIR override, so that CI's "Check the
// install rules" step can compile a program that says `uses InstallProbeUnit;`
// against a real installed prefix and prove the DEFAULT search path
// (unitSearchPaths() in plang/Basic/UnitSearchPath.h: <exeDir>/../lib/plang/units)
// actually resolves it -- the tier lit's own suite cannot reach, since lit
// always runs the in-tree build-dir binary, which has no "installed" layout
// of its own.
//
// A real standard-library unit (Crt, Dos, Printer, Strings, ...) is a later
// cluster's job, not this one's; nothing here should be mistaken for it, or
// grown into it in place.  Keep this unit exactly this small.
unit InstallProbeUnit;

interface

const InstallProbeAnswer = 42;

implementation

end.
