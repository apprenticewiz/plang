(*
Turbo Tier 5, Cluster A item 2: real Borland/FPC field practice reserves a
`_vptr` slot only when an object's hierarchy has a virtual method
SOMEWHERE (itself or an ancestor) -- an object with none costs nothing
beyond its own fields.  TRoot has no virtual method at all, so its size is
exactly its two fields' natural-alignment layout (X: Integer, 2 bytes under
-std=turbo, at offset 0; Y: Char, 1 byte, at offset 2; rounded up to the
2-byte alignment Integer sets) -- 4 bytes, not 4 + 8 for a pointer nobody
asked for.  TWithVirtual adds one virtual method and nothing else, so its
own size is X's 2 bytes, padded to the vptr's own 8-byte alignment (offset
8), plus the vptr itself (8 bytes) = 16.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TRoot = object
    X: Integer;
    Y: Char;
  end;
  TWithVirtual = object
    X: Integer;
    procedure M; virtual;
  end;
var
  r: TRoot;
  v: TWithVirtual;
procedure TWithVirtual.M;
begin
end;
begin
  writeln(SizeOf(r), ' ', SizeOf(v));
end.

(*
CHECK:4 16
*)
