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

} // namespace gobe
