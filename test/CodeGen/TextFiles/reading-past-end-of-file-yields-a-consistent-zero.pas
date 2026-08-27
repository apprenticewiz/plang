(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:first=42 second=0
*)

(* issue #284: read(f, i) past the end of a one-record file used to leave i
   holding whatever it held before the call -- the first read's value on a
   second call, or uninitialized storage on a first -- instead of a defined
   result.  conformance.md documents reading at end-of-file as unreported
   (ISO Sect 5.1 f) permits it), but that silent stale-data behavior was
   inconsistent with the runtime's own char read (plang_read_file_char) and
   binary read (plang_read_binary), which already zero the destination at
   end-of-file; numeric text reads now do the same. *)
program p;
var f: text; i: integer;
begin
  rewrite(f);
  writeln(f, '42');
  reset(f);
  read(f, i);
  write('first=', i);
  read(f, i);
  writeln(' second=', i)
end.
