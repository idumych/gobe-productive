#include "ChartPart.h"

#include <algorithm>
#include <cmath>

#include "DocWriter.h"

namespace gobe {

namespace {

std::vector<std::string>
ReadStrings(DocArchive& a)
{
	std::vector<std::string> list;
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++)
		list.push_back(a.String());
	return list;
}

} // namespace


void
ReadChartPart(DocArchive& a, ChartPart& out)
{
	ReadGRPart(a, out.graphics);
	out.sectionStart = a.Position();
	a.CheckSig(Sig("cht+"));

	// TDataAccessor::TDataAccessor(TInputArchive&, TChartPart*)
	ChartData& d = out.data;
	a.CheckSig(Sig("daa+"));
	d.link = a.UInt32();
	d.values[0] = a.UInt32();
	d.values[1] = a.UInt32();
	if (a.FileFormatVersion() >= 0x15) {
		d.values[2] = a.UInt32();
		d.values[3] = a.UInt32();
	}
	d.values[4] = a.UInt32();
	d.names1 = ReadStrings(a);
	d.names2 = ReadStrings(a);
	d.value40 = a.UInt32();
	d.value44 = a.UInt32();
	d.flag4c = a.UInt32() != 0;
	a.CheckSig(Sig("tzt+"));		// TZTable::Read
	uint32_t rows = a.UInt32();
	for (uint32_t i = 0; i < rows; i++) {
		std::vector<float> row;
		uint32_t n = a.UInt32();
		for (uint32_t k = 0; k < n; k++)
			row.push_back(a.Float());
		d.table.push_back(std::move(row));
	}
	a.CheckSig(Sig("tzt-"));
	a.CheckSig(Sig("daa-"));
	out.chartType = a.UInt32();

	// TChart::Read (the chart subclasses read their axes etc. after it)
	a.CheckSig(Sig("cht+"));
	a.CheckSig(Sig("lgn+"));
	a.Rect(out.legendRect);
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		ChartSeries series;
		a.CheckSig(Sig("ser+"));
		series.name = a.String();
		series.shown = a.Bool();
		series.style = a.UInt32();
		a.CheckSig(Sig("ser-"));
		out.series.push_back(std::move(series));
	}
	a.CheckSig(Sig("lgn-"));
	out.chartValue8 = a.UInt32();
	// TOrderTable<TSurfaceLabel>: none in the documents seen; a table with
	// labels is kept from its count to the u32 and 'cht-' that end TChart
	size_t labelsAt = a.Position();
	uint32_t labels = a.UInt32();
	const std::vector<uint8_t>& bytes = a.Data();
	auto sigAt = [&](size_t at, uint32_t sig) {
		return at + 4 <= bytes.size() && (uint32_t(bytes[at]) << 24 | bytes[at + 1] << 16 | bytes[at + 2] << 8
			| bytes[at + 3]) == sig;
	};
	if (labels != 0) {
		size_t at = a.Position();
		while (at + 8 <= bytes.size() && !sigAt(at + 4, Sig("cht-")))
			at++;
		a.Seek(at);
	}
	out.surfaceLabels.assign(bytes.begin() + long(labelsAt), bytes.begin() + long(a.Position()));
	out.chartFlags = a.UInt32();
	a.CheckSig(Sig("cht-"));

	// the part's closing 'cht-' (the part tail, 'REFT', follows it)
	size_t end = a.Position();
	while (end + 8 <= bytes.size() && !(sigAt(end, Sig("cht-")) && sigAt(end + 4, Sig("REFT"))))
		end++;
	if (end + 8 > bytes.size())
		throw ArchiveError("chart part not closed", a.Position());
	size_t values = a.FileFormatVersion() >= 0x11 ? 8 : 0;
	if (out.chartType == Sig("2DSM")) {
		// T2DXYAxisChart::Write: the axes (TAxis::Write), the series kind,
		// the sets (each its kind and T2DXYSeriesSet::Write)
		for (ChartAxis& axis : out.axes) {
			a.CheckSig(Sig("axi+"));
			for (uint32_t& v : axis.values)
				v = a.UInt32();
			axis.style = a.UInt32();
			axis.flags[0] = a.Bool();
			axis.flags[1] = a.Bool();
			a.CheckSig(Sig("axi-"));
		}
		out.seriesKind = a.UInt32();
		uint32_t sets = a.UInt32();
		for (uint32_t i = 0; i < sets; i++) {
			ChartSeriesSet set;
			set.kind = a.UInt32();
			uint32_t n = a.UInt32();
			for (uint32_t k = 0; k < n; k++)
				set.series.push_back(a.UInt32());
			out.sets.push_back(set);
		}
		if (a.Position() + values != end)
			throw ArchiveError("2DSM chart's end", a.Position());
	} else if (out.chartType == Sig("2DXY")) {
		// T2DXYChart::Write: the axes, its minor type, the series styled
		for (ChartAxis& axis : out.axes) {
			a.CheckSig(Sig("axi+"));
			for (uint32_t& v : axis.values)
				v = a.UInt32();
			axis.style = a.UInt32();
			axis.flags[0] = a.Bool();
			axis.flags[1] = a.Bool();
			a.CheckSig(Sig("axi-"));
		}
		if (a.FileFormatVersion() >= 0x13) {
			out.xyMinor = a.UInt32();
			out.xyStyled = a.UInt32();
		}
		if (a.Position() + values != end)
			throw ArchiveError("2DXY chart's end", a.Position());
	} else if (out.Is3DAxis()) {
		// T3DXYAxisChart::Write: the axes (T3DAxis::Write, each with its depth)
		for (int i = 0; i < 2; i++) {
			ChartAxis& axis = out.axes[i];
			a.CheckSig(Sig("axi+"));
			for (uint32_t& v : axis.values)
				v = a.UInt32();
			axis.style = a.UInt32();
			axis.flags[0] = a.Bool();
			axis.flags[1] = a.Bool();
			a.CheckSig(Sig("axi-"));
			out.axisDepth[i] = a.Float();
		}
		if (a.Position() + values != end)
			throw ArchiveError("3D chart's end", a.Position());
	} else if (out.chartType == Sig("PIE3")) {
		uint32_t count = a.UInt32();
		for (uint32_t i = 0; i < count; i++) {
			ChartPart::Explosion explosion;
			explosion.out = a.Float();
			explosion.up = a.Float();
			out.explosions.push_back(explosion);
		}
		if (a.Position() + values != end)
			throw ArchiveError("pie chart's end", a.Position());
	} else {
		if (end < a.Position() + values)
			throw ArchiveError("chart body", a.Position());
		out.chartBody.assign(bytes.begin() + long(a.Position()), bytes.begin() + long(end - values));
		a.Seek(end - values);
	}
	if (values != 0) {
		out.partValues[0] = a.UInt32();
		out.partValues[1] = a.UInt32();
	}
	a.CheckSig(Sig("cht-"));
	out.sectionEnd = a.Position();
}


std::vector<uint8_t>
WriteChartSection(const ChartPart& part, int fileFormat)
{
	DocWriter w;
	w.Sig(Sig("cht+"));
	// TDataAccessor::Write
	const ChartData& d = part.data;
	w.Sig(Sig("daa+"));
	w.UInt32(d.link);
	w.UInt32(d.values[0]);
	w.UInt32(d.values[1]);
	if (fileFormat >= 0x15) {
		w.UInt32(d.values[2]);
		w.UInt32(d.values[3]);
	}
	w.UInt32(d.values[4]);
	for (const std::vector<std::string>* names : {&d.names1, &d.names2}) {
		w.UInt32(uint32_t(names->size()));
		for (const std::string& name : *names)
			w.String(name);
	}
	w.UInt32(d.value40);
	w.UInt32(d.value44);
	w.UInt32(d.flag4c ? 1 : 0);
	w.Sig(Sig("tzt+"));
	w.UInt32(uint32_t(d.table.size()));
	for (const std::vector<float>& row : d.table) {
		w.UInt32(uint32_t(row.size()));
		for (float v : row)
			w.Float(v);
	}
	w.Sig(Sig("tzt-"));
	w.Sig(Sig("daa-"));
	w.UInt32(part.chartType);
	// TChart::Write
	w.Sig(Sig("cht+"));
	w.Sig(Sig("lgn+"));
	w.Rect(part.legendRect);
	w.UInt32(uint32_t(part.series.size()));
	for (const ChartSeries& series : part.series) {
		w.Sig(Sig("ser+"));
		w.String(series.name);
		w.Bool(series.shown);
		w.UInt32(series.style);
		w.Sig(Sig("ser-"));
	}
	w.Sig(Sig("lgn-"));
	w.UInt32(part.chartValue8);
	if (part.surfaceLabels.empty())
		w.UInt32(0);
	else
		w.Bytes(part.surfaceLabels);
	w.UInt32(part.chartFlags);
	w.Sig(Sig("cht-"));
	if (part.chartType == Sig("2DSM")) {
		for (const ChartAxis& axis : part.axes) {
			w.Sig(Sig("axi+"));
			for (uint32_t v : axis.values)
				w.UInt32(v);
			w.UInt32(axis.style);
			w.Bool(axis.flags[0]);
			w.Bool(axis.flags[1]);
			w.Sig(Sig("axi-"));
		}
		w.UInt32(part.seriesKind);
		w.UInt32(uint32_t(part.sets.size()));
		for (const ChartSeriesSet& set : part.sets) {
			w.UInt32(set.kind);
			w.UInt32(uint32_t(set.series.size()));
			for (uint32_t s : set.series)
				w.UInt32(s);
		}
	} else if (part.chartType == Sig("2DXY")) {
		for (const ChartAxis& axis : part.axes) {
			w.Sig(Sig("axi+"));
			for (uint32_t v : axis.values)
				w.UInt32(v);
			w.UInt32(axis.style);
			w.Bool(axis.flags[0]);
			w.Bool(axis.flags[1]);
			w.Sig(Sig("axi-"));
		}
		if (fileFormat >= 0x13) {
			w.UInt32(part.xyMinor);
			w.UInt32(part.xyStyled);
		}
	} else if (part.Is3DAxis()) {
		for (int i = 0; i < 2; i++) {
			const ChartAxis& axis = part.axes[i];
			w.Sig(Sig("axi+"));
			for (uint32_t v : axis.values)
				w.UInt32(v);
			w.UInt32(axis.style);
			w.Bool(axis.flags[0]);
			w.Bool(axis.flags[1]);
			w.Sig(Sig("axi-"));
			w.Float(part.axisDepth[i]);
		}
	} else if (part.chartType == Sig("PIE3")) {
		w.UInt32(uint32_t(part.explosions.size()));
		for (const ChartPart::Explosion& explosion : part.explosions) {
			w.Float(explosion.out);
			w.Float(explosion.up);
		}
	} else
		w.Bytes(part.chartBody);
	if (fileFormat >= 0x11) {
		w.UInt32(part.partValues[0]);
		w.UInt32(part.partValues[1]);
	}
	w.Sig(Sig("cht-"));
	return w.Data();
}


size_t
ChartPart::SeriesCount() const
{
	size_t physical = data.values[1];
	switch (data.value44) {
		case 1:
			return physical / 2;
		case 2:
			return physical > 0 ? physical - 1 : 0;
		default:
			return physical;
	}
}


size_t
ChartPart::PhysicalSeries(size_t s, int which) const
{
	switch (data.value44) {
		case 1:
			return s * 2 + size_t(which);
		case 2:
			return which != 0 ? s + 1 : 0;
		default:
			return s;
	}
}


bool
ChartPart::Is3D() const
{
	return Is3DAxis() || chartType == Sig("PIE3");
}


bool
ChartPart::Is3DAxis() const
{
	return chartType == Sig("BAR3") || chartType == Sig("ARE3") || chartType == Sig("LIN3");
}


float
ChartPart::Value(size_t s, size_t c) const
{
	size_t index = s * Categories() + c;
	size_t at = 0;
	for (const auto& row : data.table) {
		if (index < at + row.size())
			return row[index - at];
		at += row.size();
	}
	return 0;
}


namespace {

const Shape*
RenderingPiece(const ChartPart& chart)
{
	for (const GRLayer& layer : chart.graphics.layers)
		for (const Shape& shape : layer.shapes)
			if (shape.kind == Sig("Rndr"))
				return &shape;
	return nullptr;
}


struct Vector {
	float x, y, z;
};


Vector
Minus(const float a[3], const float b[3])
{
	return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}


// TVector::Normalize
Vector
Normalized(Vector v)
{
	float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	if (length != 0) {
		v.x /= length;
		v.y /= length;
		v.z /= length;
	}
	return v;
}


// NormalCrossProduct
Vector
NormalCross(Vector a, Vector b)
{
	return Normalized({a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x});
}

} // namespace


bool
ReadChartCamera(const ChartPart& chart, ChartCamera& camera)
{
	const Shape* piece = RenderingPiece(chart);
	if (piece == nullptr || piece->pieceData.size() < 4 + 18 * 4)
		return false;
	try {
		DocArchive a(piece->pieceData);
		a.CheckSig(Sig("rnp+"));
		for (float* vector : {camera.position, camera.look, camera.up})
			for (int i = 0; i < 3; i++)
				vector[i] = a.Float();
		for (int i = 0; i < 3; i++)
			camera.light[i] = a.Float();
		for (int i = 0; i < 6; i++)
			a.Float();
		if (a.Position() + 4 <= piece->pieceData.size())
			camera.lightID = a.UInt32();
		if (a.Position() < piece->pieceData.size())
			camera.lightOff = a.Bool();
	} catch (...) {
		return false;
	}
	return true;
}


void
WriteChartCamera(ChartPart& chart, const ChartCamera& camera)
{
	for (GRLayer& layer : chart.graphics.layers)
		for (Shape& shape : layer.shapes) {
			if (shape.kind != Sig("Rndr") || shape.pieceData.size() < 4 + 18 * 4)
				continue;
			DocWriter w;
			for (const float* vector : {camera.position, camera.look, camera.up, camera.light})
				for (int i = 0; i < 3; i++)
					w.Float(vector[i]);
			std::copy(w.Data().begin(), w.Data().end(), shape.pieceData.begin() + 4);
		}
}


float
ChartCameraDistance(const ChartCamera& camera)
{
	Vector v = Minus(camera.look, camera.position);
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}


void
MoveChartCamera(ChartCamera& camera, int tool, float dx, float dy, float distance)
{
	Vector view = Minus(camera.look, camera.position);
	Vector up = {camera.up[0], camera.up[1], camera.up[2]};
	if (tool == 0) {
		Vector d = Normalized(view);
		float by = (dx - dy) * 0.005f;
		camera.position[0] += d.x * by;
		camera.position[1] += d.y * by;
		camera.position[2] += d.z * by;
	} else if (tool == 1) {
		Vector side = NormalCross(view, up);
		Vector above = NormalCross(view, side);
		Vector by = {side.x * (dx * 0.005f) + above.x * (dy * 0.005f),
			side.y * (dx * 0.005f) + above.y * (dy * 0.005f),
			side.z * (dx * 0.005f) + above.z * (dy * 0.005f)};
		camera.position[0] += by.x;
		camera.position[1] += by.y;
		camera.position[2] += by.z;
		camera.look[0] += by.x;
		camera.look[1] += by.y;
		camera.look[2] += by.z;
	} else if (tool == 2) {
		Vector side = NormalCross(view, up);
		Vector above = NormalCross(view, side);
		camera.position[0] += side.x * (dx * 0.005f) + above.x * (dy * 0.005f);
		camera.position[1] += side.y * (dx * 0.005f) + above.y * (dy * 0.005f);
		camera.position[2] += side.z * (dx * 0.005f) + above.z * (dy * 0.005f);
		// (the original scales the position itself, not from what it looks at)
		float now = ChartCameraDistance(camera);
		if (now != 0)
			for (float& v : camera.position)
				v *= distance / now;
	}
}


bool
ChartCameraKey(ChartCamera& camera, uint8_t key)
{
	switch (key) {
		case 0x1c:
			camera.position[0] -= 0.02f;
			return true;
		case 0x1d:
			camera.position[0] += 0.02f;
			return true;
		case 0x1e:
			camera.position[1] -= 0.02f;
			return true;
		case 0x1f:
			camera.position[1] += 0.02f;
			return true;
		case '-':
			camera.position[2] += 0.02f;
			return true;
		case '=':
			camera.position[2] -= 0.02f;
			return true;
		case 'a':
			camera.light[2] -= 0.02f;
			return true;
		case 'z':
			camera.light[2] += 0.02f;
			return true;
	}
	return false;
}


std::vector<std::array<float, 2>>
ChartArcPoints(float radius, float start, float sweep)
{
	// (the first quarter from the right going down, as the original's pies
	// show their first slice)
	const std::array<float, 2> corners[4] = {{radius, -radius}, {-radius, -radius},
		{-radius, radius}, {radius, radius}};
	const int shift = 3;
	const float scale = float(1 << shift);
	const int steps = (0x19220 << shift) >> 16;
	std::vector<std::array<float, 2>> points;
	int last = int((start + sweep) / 90) - int(start / 90);
	int quarter = int(start / 90) % 4;
	for (int i = 0; i <= last; i++) {
		float from = i == 0 ? float(std::fmod(start, 90.0f)) : 0;
		float to = i == last ? float(std::fmod(start + sweep, 90.0f)) : 90;
		const std::array<float, 2>& corner = corners[quarter];
		const std::array<float, 2>& before = corners[(quarter + 3) % 4];
		const std::array<float, 2>& after = corners[(quarter + 1) % 4];
		float p1[2] = {(corner[0] + before[0]) / 2, (corner[1] + before[1]) / 2};
		float p3[2] = {(corner[0] + after[0]) / 2, (corner[1] + after[1]) / 2};
		float x[2] = {corner[0] - p3[0], corner[1] - p3[1]};
		float y[2] = {corner[0] - p1[0], corner[1] - p1[1]};
		float center[2] = {p1[0] - x[0], p1[1] - x[1]};
		// (the second vector drawn in half a step: Minsky's circle starts on it)
		float big = float(1 << (2 * shift + 3));
		for (int k = 0; k < 2; k++) {
			float d = y[k] / big;
			y[k] -= d;
			y[k] -= d / float(1 << (2 * shift + 4));
			y[k] -= d / big;
			y[k] += x[k] / float(1 << (shift + 1));
		}
		int toStep = int((90 - to) * float(steps) / 90);
		int fromStep = int((90 - from) * float(steps) / 90);
		for (int step = steps; step >= 0 && step >= toStep; step--) {
			if (step <= fromStep) {
				std::array<float, 2> point = {center[0] + x[0], center[1] + x[1]};
				bool add = true;
				if (points.size() >= 2 && point != points.back()) {
					// IsLine: three in a line are two
					const std::array<float, 2>& a = points[points.size() - 2];
					const std::array<float, 2>& b = points.back();
					if ((b[0] - a[0]) * (point[1] - a[1]) == (b[1] - a[1]) * (point[0] - a[0])) {
						points.back() = point;
						add = false;
					}
				}
				if (add)
					points.push_back(point);
			}
			for (int k = 0; k < 2; k++) {
				y[k] -= x[k] / scale;
				x[k] += y[k] / scale;
			}
		}
		quarter = (quarter + 1) % 4;
	}
	return points;
}


float
ChartPieTotal(const ChartPart& chart)
{
	float total = 0;
	if (chart.Categories() != 0)
		for (size_t s = 0; s < chart.SeriesCount(); s++)
			total += std::fabs(chart.Value(chart.PhysicalSeries(s, 0), 0));
	return total;
}

} // namespace gobe
