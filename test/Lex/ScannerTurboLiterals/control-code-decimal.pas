(*
Turbo `#code` control-character literal: '#' followed by decimal digits
names the character with that ordinal value, as a length-1 StringLit.  '#'
claims no existing grammar in any dialect -- EP's own '#' (16#FF) only ever
appears after scanNumber has already consumed a leading digit run, a
disjoint code path -- so this needs no disambiguation the way `^ctrl` does.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

#65

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "A"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)
