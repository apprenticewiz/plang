(*
The positive half of both dialect-gating regression gates in this area:
get/put/page/pack/unpack (Builtins.def Dialects = ISO7185 | ISO10206) and
the buffer variable f^ they operate through are ISO 7185's own file-buffer
model, so -std=turbo is the only dialect that may refuse any of them --
see the sibling tests get-put-page-pack-unpack-are-isos-file-buffer-model-
not-turbos.pas and file-buffer-variable-caret-is-isos-file-model-turbo-
rejects-it.pas (both under test/Driver/Turbo/).  Both ISO dialects must
keep accepting, compiling AND RUNNING all six exactly as before checkEPOnly
grew err_turbo_file_model_name and checkDeref's File arm grew its own
Opts.turbo() check.  f^ appears here in both a read position (`c := f^`)
and a write position (`f^ := 'a'`), the same two shapes the Turbo-side
negative test exercises.
*)

(*
RUN: %plang_run %s | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep_run %s | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ab 
CHECK-NEXT:123
*)

program p(output);
var
  f: text;
  c: char;
  a: array [1 .. 3] of integer;
  b: packed array [1 .. 3] of integer;
  i: integer;
begin
  rewrite(f);
  f^ := 'a'; put(f);
  f^ := 'b'; put(f);
  reset(f);
  while not eof(f) do begin c := f^; write(c); get(f) end;
  writeln;

  a[1] := 1; a[2] := 2; a[3] := 3;
  pack(a, 1, b);
  a[1] := 0; a[2] := 0; a[3] := 0;
  unpack(b, a, 1);
  for i := 1 to 3 do write(a[i]:1);
  writeln;

  (* page(f) last, on a freshly (re)generated f that is never read back:
     its only job here is to prove the call is still accepted and runs to
     completion under both ISO dialects -- its own effect on f's contents
     is not part of what get/put/f^ above already demonstrated. *)
  rewrite(f);
  page(f)
end.
