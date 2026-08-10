/// driver.cpp — plang compiler driver entry point.
///
/// Dispatches to the Pascal front end (plang -pc1) or the driver pipeline
/// depending on the first argument.

#include "plang/Driver/Driver.h"
#include "plang/Frontend/Frontend.h"

#include <string_view>

int main(int Argc, char *Argv[]) {
    // Dispatch to the Pascal front end when re-invoked with -pc1.
    if (Argc >= 2 && std::string_view(Argv[1]) == "-pc1")
        return plang::frontendPC1Main(Argc, Argv);
    // Pass Argv[0] so Driver can call getMainExecutable() portably.
    return plang::Driver(Argc > 0 ? Argv[0] : nullptr).run(Argc, Argv);
}
