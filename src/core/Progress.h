// How far an import or export has got, for the Status window (TStatusFile
// and the translators' "progress_invoker" feed TStatusDialog). The app
// installs a reporter around ImportFile and ExportFile; the importers and
// exporters call ReportProgress as they go.
#ifndef GOBE_PROGRESS_H
#define GOBE_PROGRESS_H

#include <functional>
#include <string>

namespace gobe {

// gets the fraction done (0 to 1); returns false once the user has
// cancelled (TStatusDialog::WasCancelled)
using ProgressReporter = std::function<bool(float fraction)>;

// installs `reporter` for as long as the scope lasts (the one before it
// comes back after)
class ProgressScope {
public:
	explicit ProgressScope(ProgressReporter reporter);
	~ProgressScope();

private:
	ProgressScope(const ProgressScope&) = delete;
	ProgressScope& operator=(const ProgressScope&) = delete;
	ProgressReporter fPrevious;
};

// `done` of `total` (clamped to 0..1; nothing when total is 0) to the
// installed reporter; true without one. False: stop, the user cancelled.
bool ReportProgress(double done, double total);

// the error text an importer or exporter returns when it stopped for a
// cancel (the app shows no alert for it)
extern const char* const kCancelledError;

} // namespace gobe

#endif
