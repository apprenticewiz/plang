(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
const d = 0.0625;
      s = 32;
      h = 34;
      c = 6.28318;
      lim = 32;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (const d 0.0625)
CHECK-NEXT:  (const s 32)
CHECK-NEXT:  (const h 34)
CHECK-NEXT:  (const c 6.28318)
CHECK-NEXT:  (const lim 32)
CHECK-NEXT:  (compound))
*)
