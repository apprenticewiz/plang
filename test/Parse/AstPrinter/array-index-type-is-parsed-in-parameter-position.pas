(*
RUN: %plang_ir -dump-parse-tree -std=iso10206 %s | FileCheck --strict-whitespace --match-full-lines %s
*)

(* Issue #258: a formal parameter's array type routed a regular (non-schema)
   index through parseConformantOrRegular's own hand-rolled lo..hi
   speculation instead of parseArrayIndexType, so any index that is not an
   integer subrange range -- boolean, char, a named enum type, or an inline
   enumeration -- was mis-parsed as a broken expression even though the
   identical denoter in a `type` or `var` declaration always parsed fine. *)

program p;
type Color = (red, green, blue);
procedure boolIdx(a: array[boolean] of integer); begin end;
procedure charIdx(a: array[char] of integer); begin end;
procedure enumIdx(a: array[Color] of integer); begin end;
procedure inlineEnumIdx(a: array[(lo, mid, hi)] of integer); begin end;
begin end.

(*
CHECK:(program p
CHECK-NEXT:  (typedef Color (enum red green blue))
CHECK-NEXT:  (procedure boolIdx ((a (array boolean integer)))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (procedure charIdx ((a (array char integer)))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (procedure enumIdx ((a (array Color integer)))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (procedure inlineEnumIdx ((a (array (enum lo mid hi) integer)))
CHECK-NEXT:    (compound))
CHECK-NEXT:  (compound))
*)
