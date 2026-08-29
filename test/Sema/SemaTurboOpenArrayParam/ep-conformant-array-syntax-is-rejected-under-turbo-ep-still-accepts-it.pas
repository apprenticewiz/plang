(*
EP/ISO 7185 Level 1's conformant-array-schema form (array [lo..hi: T] of E)
never existed in real Turbo Pascal -- confirmed with a local fpc -Mtp
build, which rejects it outright -- so it must stay rejected under
-std=turbo even though this feature reuses the exact same TypeKind
(ConformantArray) Turbo's own open-array form does; this is NOT already
covered by an existing EP-only gate (there was none for this specific
parameter form before this feature -- parseParamGroup's own comment).
Extended Pascal itself must keep accepting it completely unaffected -- same
diagnostic-free compile as before Turbo's open array existed.

RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck --check-prefix=TURBO-REJECTS %s < %t.err
RUN: %plang -std=iso10206 -dump-ast %s > %t.ast
*)

program p;
procedure Foo(a: array[lo .. hi: Integer] of Integer);
begin
end;

begin
end.

(*
TURBO-REJECTS: Turbo Pascal has no conformant array parameters (array[lo..hi: T] of E); use an open array parameter (array of T) instead
*)
