// Culling and exporting a shoot without a window: the same Backend and
// EngineService the interface drives, given a small vocabulary and asked to
// report in JSON so something other than a person can read the answer.
//
//   omaraw --catalog X.db [--new-catalog] import <folder>
//                         [--mode add|copy|move|verify] [--dest DIR]
//   omaraw --catalog X.db list [filters]
//   omaraw --catalog X.db export <folder> [filters] [--format jpeg] [--quality 90]
//   omaraw --catalog X.db ops|inspect|apply …   everything else, by name (docs/cli.md)
//   omaraw skill [--link] [--force]             the agent skill, and where it is linked
//
// A verb is what makes a run headless; without one the application opens a
// window as usual. --headless forces the verb reading for the one case that
// is ambiguous, a folder named after a verb.
//
// Filters are the browser's own: --rating N (at least N), --flag pick|reject,
// --label red|…, --text "…". `list` and `export` see exactly what the Library
// would show under them.
#pragma once

#include <QString>
#include <QStringList>

class Backend;
class EngineService;
class QCommandLineParser;

namespace Headless {

// Keep stdout reserved for JSON from before engine startup until after its
// worker shuts down. Construct after command-line help/version processing.
class OutputGuard {
public:
    explicit OutputGuard(bool enabled);
    ~OutputGuard();
    OutputGuard(const OutputGuard &) = delete;
    OutputGuard &operator=(const OutputGuard &) = delete;
private:
    int m_saved = -1;
};

// Whether the command line asks for headless work. Read from argv before Qt
// is constructed, because the platform plugin has to be chosen first.
bool requested(int argc, char *argv[]);

// The verbs and their options, added to the parser the application shares.
void addOptions(QCommandLineParser &parser);

// Runs the verb against an open catalog and, for export, a started engine.
// Writes one JSON object to stdout. Returns the process exit code:
// 0 the work was done, 1 the work failed, 2 the command was not understood.
int run(const QCommandLineParser &parser, Backend &backend, EngineService &engine);

// Reports a refusal in the same JSON shape as a finished run, for failures
// that happen before there is a catalog to work in.
int fail(const QString &message);

// Verbs that need no catalog (`skill`), run before one is opened. Returns
// the exit code, or -1 when the verb is not one of them.
int early(const QCommandLineParser &parser);

} // namespace Headless
