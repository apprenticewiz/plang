(*
Gap 1 regression gate.  get/put/page/pack/unpack are Builtins.def's Dialects
= ISO (ISO7185 | ISO10206) group -- the file-buffer model ISO 7185 itself
defines and -std=turbo replaces with Assign/Seek rather than extending, the
same way -std=turbo replaces the whole get/put/page buffer-variable model
(see the sibling test for `f^` itself). checkEPOnly used to route this
group through err_ep_required_name, whose wording ("is an Extended Pascal
extension and is not available under -std=iso7185") is wrong twice over
here: these five are not Extended Pascal's alone (iso7185 has them too), and
the dialect actually running this file is -std=turbo, not -std=iso7185.
checkEPOnly now recognises Dialects == (D_ISO7185 | D_ISO10206) as its own
case and reports err_turbo_file_model_name instead.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'get' is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
CHECK: 'put' is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
CHECK: 'page' is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
CHECK: 'pack' is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
CHECK: 'unpack' is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
*)

program p;
var
  f: text;
  a: array [1 .. 3] of integer;
  b: packed array [1 .. 3] of integer;
begin
  get(f);
  put(f);
  page(f);
  pack(a, 1, b);
  unpack(b, a, 1);
end.
