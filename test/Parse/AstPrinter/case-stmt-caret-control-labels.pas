(*
Issue #600: a case-statement's own 'of' always introduces case-labels,
never a type-denoter -- unlike `array of`/`set of`/`file of`'s own 'of'
(ParseType.cpp), which must NOT get this treatment. Parser::parseCaseStmt
arms Scanner::allowCaretControlCharNext() immediately before consuming
'of', and again before each ','/';'/'..' that introduces a further label,
so `^letter` reads as a control-character constant in every one of a
case-statement's own label positions -- proven here by dumping the parse
tree: `^A`/`^M` must show up as StringLit labels holding their real
ordinal values (1 and 13, the literal control bytes below -- not the
two-character spelling '^A'/'^M'), not misparsed as a Caret token
starting a deref. `-dump-parse-tree` (which builds a real Parser, unlike
`-dump-tokens`) is required here: the disambiguation is a PARSER-driven
scanner override, not a pure scanner-context decision the way Colon/Assign
already are (see caret-control-in-type-declaration-not-misparsed.pas,
ScannerTurboLiterals), so a token dump alone can never exercise it.
*)

(*
RUN: %plang_ir -dump-parse-tree -std=turbo %s | FileCheck %s
*)

program p; var c: char; i: integer; begin case c of ^A: i := 1; ^M: i := 2 end end.

(*
CHECK: (case c
CHECK: (arm ("")
CHECK: (arm ("")
*)
