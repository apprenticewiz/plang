(*
Turbo Tier 4, Cluster C item 6: Dos.Exec is synchronous fork+exec+waitpid
(runtime/plang_dos.cpp's own plang_dos_exec) -- confirmed against real
`fpc -Mtp` field practice (rtl/unix/dos.pp's own Exec) that the calling
program blocks until the child exits, and that the child's own exit code
is read back afterward through a SEPARATE function, DosExitCode, not a var
parameter of Exec itself.  Runs a real, tiny fixture SHELL SCRIPT this RUN
line creates (rather than depending on a fixed system binary's own exact
argv-echoing behaviour), which writes a known marker to a real output file
and exits with a specific nonzero status -- checked here from BOTH sides:
the marker file's real contents, and DosExitCode's own value.

RUN: rm -rf %t.dir && mkdir -p %t.dir
RUN: printf '#!/bin/sh\necho ran-ok-$1 > %t.dir/marker.txt\nexit 7\n' > %t.dir/script.sh
RUN: chmod +x %t.dir/script.sh
RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t %t.dir/script.sh %t.dir/marker.txt | FileCheck %s
RUN: FileCheck --check-prefix=MARKER %s < %t.dir/marker.txt
*)

program DosExec;
uses Dos;
begin
  Exec(ParamStr(1), 'hello');
  Writeln('doserror: ', DosError);
  Writeln('exitcode: ', DosExitCode);
end.

(*
CHECK:doserror: 0
CHECK-NEXT:exitcode: 7
MARKER:ran-ok-hello
*)
