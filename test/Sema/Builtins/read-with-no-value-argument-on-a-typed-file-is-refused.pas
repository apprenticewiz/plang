(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* Issue #674: `read(f)` on a typed file, with no value argument at all,
   used to be silently accepted as a no-op -- CodeGen's emitBuiltinRead
   loop (BuiltinIO.cpp) simply never ran for it.  Confirmed against a
   local fpc -Mtp 3.2.2 install: it refuses this with "Wrong number of
   parameters specified for call to Read".  `read(g)` on a TEXT file with
   no value argument (line 2 here) is a deliberate no-op both in fpc and
   in plang, so it is NOT refused -- only the typed-file case is. *)
program p;
var
  t: file of Integer;
  g: Text;
begin
  read(g);
  read(t);
end.

(*
CHECK-NOT: :18:
CHECK: :19:3: error: 'read' expects 2 or more argument(s), got 1
*)
