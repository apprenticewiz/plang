(*
Issue #600: a const-DEFINITION's own '=' always introduces a value, never
a type -- unlike a type-DEFINITION's '=' (`type PM = ^Integer`, see
caret-control-in-type-declaration-not-misparsed.pas, ScannerTurboLiterals,
which must keep meaning a pointer type and NOT get this treatment).
Parser::parseConstDef arms Scanner::allowCaretControlCharNext()
immediately before consuming '=', so `^letter` right after it
(`const CR = ^M;`) reads as a control-character constant rather than a
Caret token starting a (senseless, at the very start of a value) pointer
dereference. `-dump-parse-tree` (which builds a real Parser, unlike
`-dump-tokens`) is required: this is a PARSER-driven scanner override,
not a pure scanner-context decision, so a token dump alone can never
exercise it. The control byte below (13, a literal carriage return) is
the real ordinal value `^M` denotes -- not its two-character spelling.
*)

(*
RUN: %plang_ir -dump-parse-tree -std=turbo %s | FileCheck %s
*)

program p; const CR = ^M; begin end.

(*
CHECK: (const CR "")
*)
