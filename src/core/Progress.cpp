#include "Progress.h"

#include <utility>

namespace gobe {

namespace {

ProgressReporter sReporter;

} // namespace


const char* const kCancelledError = "cancelled";


ProgressScope::ProgressScope(ProgressReporter reporter)
	:
	fPrevious(std::move(sReporter))
{
	sReporter = std::move(reporter);
}


ProgressScope::~ProgressScope()
{
	sReporter = std::move(fPrevious);
}


bool
ReportProgress(double done, double total)
{
	if (!sReporter || total <= 0)
		return true;
	double fraction = done / total;
	if (fraction < 0)
		fraction = 0;
	if (fraction > 1)
		fraction = 1;
	return sReporter(float(fraction));
}


bool
ReportStep(int step, int steps, double done, double total)
{
	if (total <= 0)
		total = 1;
	return ReportProgress(step + done / total, steps);
}


bool
StepCancelled(std::string& error, int step, int steps, double done, double total)
{
	if (ReportStep(step, steps, done, total))
		return false;
	error = kCancelledError;
	return true;
}

} // namespace gobe
