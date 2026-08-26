#pragma once

#include "plang/Basic/Diagnostic.h"
#include "plang/Basic/LangOptions.h"
#include "plang/Parse/Parser.h"
#include "plang/Lex/Scanner.h"
#include "plang/Sema/Sema.h"

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

namespace plang {

class TempFile {
public:
    explicit TempFile(const std::string &Content) {
        char Tmpl[] = "/tmp/plang_test_XXXXXX";
        int Fd = mkstemp(Tmpl);
        Path = Tmpl;
        write(Fd, Content.data(), Content.size());
        close(Fd);
    }
    ~TempFile() { std::remove(Path.c_str()); }
    const std::string &path() const { return Path; }
private:
    std::string Path;
};

struct SemaResult {
    bool                    Ok;
    std::vector<Diagnostic> Diags;

    int errorCount() const {
        int N = 0;
        for (const auto &D : Diags)
            if (D.Severity == DiagSeverity::Error) ++N;
        return N;
    }
    int warningCount() const {
        int N = 0;
        for (const auto &D : Diags)
            if (D.Severity == DiagSeverity::Warning) ++N;
        return N;
    }
    bool hasError(const std::string &Sub) const {
        for (const auto &D : Diags)
            if (D.Severity == DiagSeverity::Error &&
                D.Message.find(Sub) != std::string::npos) return true;
        return false;
    }
    bool hasWarning(const std::string &Sub) const {
        for (const auto &D : Diags)
            if (D.Severity == DiagSeverity::Warning &&
                D.Message.find(Sub) != std::string::npos) return true;
        return false;
    }
};

/// Compile Src through Scanner->Parser->Sema in-process and return the result.
/// All phases share one DiagnosticsEngine; no exceptions are used.
/// Opts defaults to ISO 7185 so existing callers need not change; DiagOpts
/// lets a test exercise -w, -Werror and -Wno-<name>.
inline SemaResult check(const std::string &Src, LangOptions Opts = {},
                        DiagnosticOptions DiagOpts = {}) {
    TempFile F(Src);
    DiagnosticsEngine Diags(std::move(DiagOpts));
    SourceManager     SM;
    Scanner Scanner(SM, F.path(), Diags, Opts);
    Parser  Parser(std::move(Scanner), Diags, Opts);
    auto    Prog = Parser.parse();
    if (!Prog) return { false, Diags.diagnostics() };
    Sema    Sema(Diags, Opts);
    bool    Ok = Sema.check(*Prog);
    return { Ok, Diags.diagnostics() };
}

} // namespace plang
