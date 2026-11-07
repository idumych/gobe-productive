#include "TChartGL.h"

#include <GLView.h>
#include <GL/gl.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

#include "DocWriter.h"

namespace gobe {

namespace {

// B_TRANSPARENT_COLOR: what's clear of the chart (TChartView::EnforceState)
const uint8 kClear[3] = {0x77, 0x74, 0x77};


// TChartView: a BGLView off screen, its projection for its size
// (TChartView::TChartView, FrameResized, EnforceState)
class TChartGLView : public BGLView {
public:
	TChartGLView(BRect frame)
		:
		BGLView(frame, "ChartView", B_FOLLOW_ALL_SIDES, 0, BGL_RGB | BGL_DOUBLE | BGL_DEPTH),
		fWidth(frame.Width()),
		fHeight(frame.Height())
	{
	}

	void SetUp()
	{
		glViewport(0, 0, GLsizei(fWidth) + 1, GLsizei(fHeight) + 1);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		// gluPerspective(60, width / height, 0.15, 120)
		double aspect = fWidth / fHeight, zNear = 0.15, zFar = 120;
		double top = zNear * tan(30 * M_PI / 180);
		glFrustum(-top * aspect, top * aspect, -top, top, zNear, zFar);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	void EnforceState()
	{
		glClearColor(kClear[0] / 255.0f, kClear[1] / 255.0f, kClear[2] / 255.0f, 1);
		glShadeModel(GL_SMOOTH);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_LIGHTING);
		// (BeOS's OpenGL lights by unit normals: T3DAreaChart's front, given
		// longer ones, comes out as there; the other charts' are unit ones)
		glEnable(GL_NORMALIZE);
	}

	float WidthOf() const { return fWidth; }
	float HeightOf() const { return fHeight; }

private:
	float fWidth, fHeight;
};


struct GLHolder {
	TChartGLView* view = NULL;
	bool ready = false;
};


// the window never shown that the views are in (not made while another
// window draws: PrepareChartGL)
BWindow*
GLWindow()
{
	static BWindow* window = NULL;
	if (window == NULL) {
		window = new BWindow(BRect(-30000, -30000, -28000, -28000), "ChartView",
			B_NO_BORDER_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
			B_NOT_MOVABLE | B_NOT_RESIZABLE | B_AVOID_FOCUS);
		window->Hide();
		window->Show();
	}
	return window;
}


// a view for each size in use
GLHolder*
HolderFor(int32 width, int32 height)
{
	static std::map<std::pair<int32, int32>, GLHolder> holders;
	std::pair<int32, int32> size(width, height);
	auto found = holders.find(size);
	if (found != holders.end())
		return &found->second;
	BWindow* window = GLWindow();
	if (!window->Lock())
		return NULL;
	if (holders.size() >= 8) {
		TChartGLView* old = holders.begin()->second.view;
		window->RemoveChild(old);
		delete old;
		holders.erase(holders.begin());
	}
	GLHolder& holder = holders[size];
	holder.view = new TChartGLView(BRect(0, 0, width - 1, height - 1));
	window->AddChild(holder.view);
	window->Unlock();
	return &holder;
}


// T3DChart::NextClickTestColor and StartClickTest; TChartDrawInfo's click test
struct DrawInfo {
	const ChartPart* chart;
	const StyleMgr* styles;
	bool clickTest = false;
	int32 selected = -1;
	int32 nextID = 0;
	GLfloat color[3] = {0, 0, 0};
};


void
NextClickTestColor(DrawInfo& info)
{
	info.color[0] = float(0xff - info.nextID % 0xffff) / 255;
	info.color[1] = float(0xff - info.nextID / 0xffff) / 255;
	info.color[2] = 0;
	info.nextID++;
}


void
Material(const GLfloat color[3])
{
	GLfloat with[4] = {color[0], color[1], color[2], 1};
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, with);
}


// T3DChart::ApplySeriesColor (TChartLegendSeries::Color: its fill)
void
ApplySeriesColor(DrawInfo& info, size_t series)
{
	if (info.clickTest) {
		NextClickTestColor(info);
		glColor3fv(info.color);
		return;
	}
	GLfloat color[3] = {0, 0, 0};
	if (series < info.chart->series.size()) {
		ShapeStyle style = ResolveShapeStyle(*info.styles, info.chart->series[series].style);
		if (style.fill.set) {
			color[0] = style.fill.color.r / 255.0f;
			color[1] = style.fill.color.g / 255.0f;
			color[2] = style.fill.color.b / 255.0f;
		}
	}
	Material(color);
}


// T3DAxis::StartDraw: its pen's color and size (at least 1), lines; in a
// click test, a strip in the test's color
void
StartAxis(DrawInfo& info, const ChartAxis& axis)
{
	if (info.clickTest) {
		glColor3fv(info.color);
		glBegin(GL_QUAD_STRIP);
		return;
	}
	ShapeStyle style = ResolveShapeStyle(*info.styles, axis.style);
	GLfloat color[3] = {0, 0, 0};
	if (style.pen.set) {
		color[0] = style.pen.color.r / 255.0f;
		color[1] = style.pen.color.g / 255.0f;
		color[2] = style.pen.color.b / 255.0f;
	}
	glLineWidth(std::max(1.0f, style.penSize));
	Material(color);
	glBegin(GL_LINES);
}


float
AsFloat(uint32_t bits)
{
	float f;
	memcpy(&f, &bits, sizeof f);
	return f;
}


// T3DVerticalAxis::Draw: a line back from each step
void
DrawVerticalAxis(DrawInfo& info)
{
	const ChartAxis& axis = info.chart->axes[1];
	float step = ChartAxisStep(axis);
	if (!(step > 0))
		return;
	StartAxis(info, axis);
	float low = AsFloat(axis.values[2]), high = AsFloat(axis.values[3]);
	float depth = -(info.chart->axisDepth[1] + 1) / 10;
	float y = 0;
	for (float v = low; v < step * 0.5f + high; v += step) {
		glVertex3f(0, y, 0);
		glVertex3f(0, y, depth);
		y += step / (high - low);
	}
	glEnd();
}


// T3DHorizontalAxis::Draw: at each tick a line back and, not in a click
// test, up; selected, a handle at each
void
DrawHorizontalAxis(DrawInfo& info)
{
	const ChartAxis& axis = info.chart->axes[0];
	StartAxis(info, axis);
	int32 ticks = int32(axis.values[6]);
	float depth = -(info.chart->axisDepth[0] + 1) * 0.1f;
	float across = 1.0f / float(ticks);
	for (int32 i = 0; i <= ticks; i++) {
		float x = across * float(i);
		glVertex3f(x, 0, 0);
		glVertex3f(x, 0, depth);
		if (!info.clickTest) {
			glVertex3f(x, 0, depth);
			glVertex3f(x, 1, depth);
		}
	}
	glEnd();
	if (info.selected == 1)
		for (int32 i = 0; i <= ticks; i++) {
			float x = across * float(i);
			glBegin(GL_POLYGON);
			glVertex3f(x - 0.02f, -0.02f, 0);
			glVertex3f(x + 0.02f, -0.02f, 0);
			glVertex3f(x + 0.02f, 0.02f, 0);
			glVertex3f(x - 0.02f, 0.02f, 0);
			glEnd();
		}
}


// T3DBarChart::DrawBar
void
DrawBar(const float at[3], const float size[3])
{
	for (int i = 0; i < 2; i++) {
		float z = float(i) * size[2] + at[2];
		glBegin(GL_POLYGON);
		glNormal3f(0, 0, i == 0 ? 1 : -1);
		glVertex3f(at[0], at[1], z);
		glVertex3f(at[0], at[1] + size[1], z);
		glVertex3f(at[0] + size[0], at[1] + size[1], z);
		glVertex3f(at[0] + size[0], at[1], z);
		glEnd();
		float x = float(i) * size[0] + at[0];
		glBegin(GL_POLYGON);
		glNormal3f(i == 0 ? -1 : 1, 0, 0);
		glVertex3f(x, at[1], at[2]);
		glVertex3f(x, at[1] + size[1], at[2]);
		glVertex3f(x, at[1] + size[1], at[2] + size[2]);
		glVertex3f(x, at[1], at[2] + size[2]);
		glEnd();
		float y = float(i) * size[1] + at[1];
		glBegin(GL_POLYGON);
		// (the original's normal here is along x too)
		glNormal3f(i == 0 ? -1 : 1, 0, 0);
		glVertex3f(at[0], y, at[2]);
		glVertex3f(at[0] + size[0], y, at[2]);
		glVertex3f(at[0] + size[0], y, at[2] + size[2]);
		glVertex3f(at[0], y, at[2] + size[2]);
		glEnd();
	}
}


// T3DVerticalAxis::ZeroPoint's height, 1 / the range
void
VerticalScale(const ChartPart& chart, float& zero, float& scale)
{
	const ChartAxis& axis = chart.axes[1];
	float niceLow = AsFloat(axis.values[2]), niceHigh = AsFloat(axis.values[3]);
	zero = -(niceLow / (niceHigh - niceLow));
	float low, high;
	ChartAxisRange(axis, low, high);
	scale = 1.0f / (high - low);
}


// T3DBarChart::DrawBars
void
DrawBars(DrawInfo& info)
{
	const ChartPart& chart = *info.chart;
	float zero, scale;
	VerticalScale(chart, zero, scale);
	size_t categories = chart.Categories();
	for (size_t s = 0; s < chart.SeriesCount(); s++) {
		ApplySeriesColor(info, s);
		float across = 1.0f / float(categories);
		float wide = across - across / 10;
		for (size_t c = 0; c < categories; c++) {
			float at[3] = {across * float(c), zero, float(~int32(s)) * 0.1f};
			float size[3] = {wide, chart.Value(s, c) * scale, -0.08f};
			DrawBar(at, size);
		}
	}
}


struct Point3 {
	float x, y, z;
};


// T3DAreaChart::ContentDraw: for each series its top, then its front
void
DrawAreas(DrawInfo& info)
{
	const ChartPart& chart = *info.chart;
	float zero, scale;
	VerticalScale(chart, zero, scale);
	size_t categories = chart.Categories();
	float half = (1.0f / float(categories)) * 0.5f;
	std::vector<GLfloat> colors;
	float z = 0;
	for (size_t s = 0; s < chart.SeriesCount(); s++, z -= 0.1f) {
		if (!info.clickTest)
			ApplySeriesColor(info, s);
		else {
			NextClickTestColor(info);
			colors.insert(colors.end(), info.color, info.color + 3);
			glColor3fv(info.color);
		}
		glBegin(GL_QUAD_STRIP);
		Point3 front = {half, chart.Value(s, 0) * scale + zero, z};
		Point3 back = {half, front.y, z - 0.075f};
		for (size_t c = 1; c < categories; c++) {
			Point3 nextFront = {(1.0f / float(categories)) * float(c) + half,
				chart.Value(s, c) * scale + zero, z};
			Point3 nextBack = {nextFront.x, nextFront.y, z - 0.075f};
			// NormalCrossProduct(front - back, back - back before)
			float a[3] = {nextFront.x - nextBack.x, nextFront.y - nextBack.y,
				nextFront.z - nextBack.z};
			float b[3] = {nextBack.x - back.x, nextBack.y - back.y, nextBack.z - back.z};
			float normal[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
				a[0] * b[1] - a[1] * b[0]};
			float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1]
				+ normal[2] * normal[2]);
			if (length != 0)
				glNormal3f(normal[0] / length, normal[1] / length, normal[2] / length);
			glVertex3f(front.x, front.y, front.z);
			glVertex3f(back.x, back.y, back.z);
			front = nextFront;
			back = nextBack;
		}
		glVertex3f(front.x, front.y, front.z);
		glVertex3f(back.x, back.y, back.z);
		glEnd();
	}
	z = 0;
	for (size_t s = 0; s < chart.SeriesCount(); s++, z -= 0.1f) {
		if (!info.clickTest)
			ApplySeriesColor(info, s);
		else
			glColor3fv(&colors[s * 3]);
		glBegin(GL_QUAD_STRIP);
		for (size_t c = 0; c < categories; c++) {
			float x = (1.0f / float(categories)) * float(c) + half;
			float y = chart.Value(s, c) * scale + zero;
			glNormal3f(x, zero, 1);
			glVertex3f(x, zero, z);
			glNormal3f(x, y, 1);
			glVertex3f(x, y, z);
		}
		glEnd();
	}
}


// T3DLineChart::ContentDraw: for each series a ribbon through its values
void
DrawLines(DrawInfo& info)
{
	const ChartPart& chart = *info.chart;
	float zero, scale;
	VerticalScale(chart, zero, scale);
	size_t categories = chart.Categories();
	float z = 0;
	for (size_t s = 0; s < chart.SeriesCount(); s++, z -= 0.1f) {
		ApplySeriesColor(info, s);
		glBegin(GL_QUAD_STRIP);
		for (size_t c = 0; c < categories; c++) {
			float x = float(c) / float(categories);
			float y = chart.Value(s, c) * scale + zero;
			glVertex3f(x, y, z);
			glVertex3f(x, y, z + 0.075f);
		}
		glEnd();
	}
}


// T3DPieChart::BuildModel for a series: the arc's faces, 0.05 deep, and
// its side
void
DrawSlice(const std::vector<std::array<float, 2>>& points)
{
	float z = 0;
	for (int i = 0; i < 2; i++) {
		z = float(-i) * 0.05f;
		glBegin(GL_POLYGON);
		glVertex3f(0, 0, z);
		for (const std::array<float, 2>& p : points)
			glVertex3f(p[0], p[1], z);
		glVertex3f(0, 0, z);
		glEnd();
	}
	glBegin(GL_QUAD_STRIP);
	glVertex3f(0, 0, 0);
	glVertex3f(0, 0, z);
	for (const std::array<float, 2>& p : points) {
		glVertex3f(p[0], p[1], 0);
		glVertex3f(p[0], p[1], z);
	}
	glVertex3f(0, 0, 0);
	glVertex3f(0, 0, z);
	glEnd();
}


// T3DPieChart::Draw: each series' slice filled in its color, then outlined
// in black, moved out by its explosion
void
DrawPie(DrawInfo& info)
{
	const ChartPart& chart = *info.chart;
	float total = ChartPieTotal(chart);
	float start = 0;
	for (size_t s = 0; s < chart.SeriesCount(); s++) {
		float value = std::fabs(chart.Value(s, 0));
		float sweep = total != 0 && value != 0 ? 360.0f / (total / value) : 0;
		std::vector<std::array<float, 2>> points = ChartArcPoints(3500, start, sweep);
		for (std::array<float, 2>& p : points) {
			p[0] /= 10000;
			p[1] /= 10000;
		}
		float middle = (start + sweep * 0.5f) * float(M_PI) / 180;
		float out = 0, up = 0;
		if (s < chart.explosions.size()) {
			out = chart.explosions[s].out;
			up = chart.explosions[s].up;
		}
		glPushMatrix();
		glTranslatef(cosf(middle) * out, sinf(-middle) * out, up);
		ApplySeriesColor(info, s);
		glPolygonMode(GL_FRONT, GL_FILL);
		glPolygonMode(GL_BACK, GL_FILL);
		DrawSlice(points);
		const GLfloat black[3] = {0, 0, 0};
		glPolygonMode(GL_FRONT, GL_LINE);
		glPolygonMode(GL_BACK, GL_LINE);
		Material(black);
		DrawSlice(points);
		glPopMatrix();
		start += sweep;
	}
}


// T3DXYAxisChart::Draw, T3DPieChart::Draw
void
DrawChart(DrawInfo& info)
{
	const ChartPart& chart = *info.chart;
	if (chart.SeriesCount() == 0 || chart.Categories() == 0)
		return;
	if (chart.chartType == Sig("PIE3")) {
		DrawPie(info);
		return;
	}
	NextClickTestColor(info);
	DrawVerticalAxis(info);
	if (info.clickTest)
		NextClickTestColor(info);
	DrawHorizontalAxis(info);
	glPolygonMode(GL_FRONT, GL_FILL);
	glPolygonMode(GL_BACK, GL_FILL);
	if (chart.chartType == Sig("BAR3"))
		DrawBars(info);
	else if (chart.chartType == Sig("ARE3"))
		DrawAreas(info);
	else
		DrawLines(info);
}


struct Vector3 {
	double x, y, z;
};


Vector3
Normalized(Vector3 v)
{
	double length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	if (length == 0)
		return v;
	return {v.x / length, v.y / length, v.z / length};
}


Vector3
Cross(Vector3 a, Vector3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}


// T3DRenderer::Render: the light (TLight::LightSceen), the camera
// (TCamera::Snap: gluLookAt)
void
SnapLightAndCamera(const ChartCamera& camera)
{
	if (!camera.lightOff)
		glEnable(camera.lightID);
	const GLfloat white[4] = {1, 1, 1, 1};
	glLightfv(camera.lightID, GL_DIFFUSE, white);
	const GLfloat place[4] = {camera.light[0], camera.light[1], camera.light[2], 1};
	glLightfv(camera.lightID, GL_POSITION, place);
	Vector3 f = Normalized({double(camera.look[0]) - camera.position[0],
		double(camera.look[1]) - camera.position[1],
		double(camera.look[2]) - camera.position[2]});
	Vector3 s = Normalized(Cross(f, {camera.up[0], camera.up[1], camera.up[2]}));
	Vector3 u = Cross(s, f);
	const GLdouble m[16] = {s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0, 0, 0, 0, 1};
	glMultMatrixd(m);
	glTranslated(-camera.position[0], -camera.position[1], -camera.position[2]);
}


// TruncateNumericString: a number's trailing zeros, and its point if that's
// all left of them
std::string
AxisLabel(float value)
{
	char text[64];
	int length = snprintf(text, sizeof text, "%.12g", double(value));
	bool point = false;
	for (int i = 0; i < length; i++)
		point = point || text[i] == '.';
	if (point)
		for (int i = length - 1; i >= 0; i--) {
			if (text[i] == '.') {
				text[i] = '\0';
				break;
			}
			if (text[i] != '0')
				break;
			text[i] = '\0';
		}
	return text;
}


// gluProject under the view's matrices
void
Project(double x, double y, double z, const GLdouble modelview[16],
	const GLdouble projection[16], double width, double height, double& winX, double& winY)
{
	double eye[4], clip[4];
	const double in[4] = {x, y, z, 1};
	for (int r = 0; r < 4; r++) {
		eye[r] = 0;
		for (int c = 0; c < 4; c++)
			eye[r] += modelview[c * 4 + r] * in[c];
	}
	for (int r = 0; r < 4; r++) {
		clip[r] = 0;
		for (int c = 0; c < 4; c++)
			clip[r] += projection[c * 4 + r] * eye[c];
	}
	double w = clip[3] != 0 ? clip[3] : 1;
	winX = (clip[0] / w * 0.5 + 0.5) * width;
	winY = (clip[1] / w * 0.5 + 0.5) * height;
}


// T3DVerticalAxis::DrawLabels: each value 10 left of where the axis's
// point projects
void
AxisLabels(const ChartPart& chart, float width, float height, ChartRender3D& out)
{
	if (!chart.Is3DAxis() || chart.SeriesCount() == 0 || chart.Categories() == 0)
		return;
	const ChartAxis& axis = chart.axes[1];
	float step = ChartAxisStep(axis);
	if (!(step > 0))
		return;
	GLdouble modelview[16], projection[16];
	glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
	glGetDoublev(GL_PROJECTION_MATRIX, projection);
	float low = AsFloat(axis.values[2]), high = AsFloat(axis.values[3]);
	float y = 0;
	for (float v = low; v < step * 0.5f + high; v += step) {
		double winX, winY;
		Project(-0.05f, y, 0.01f, modelview, projection, width, height, winX, winY);
		out.labels.push_back({float(winX - 10), float(height - winY), AxisLabel(v)});
		y += step / (high - low);
	}
}

} // namespace


void
PrepareChartGL()
{
	GLWindow();
}


std::string
ChartRenderKey(const ChartPart& chart, const StyleMgr& styles, int32 width, int32 height)
{
	DocWriter w;
	w.UInt32(uint32_t(width));
	w.UInt32(uint32_t(height));
	w.Bytes(WriteChartSection(chart, 0x15));
	for (const GRLayer& layer : chart.graphics.layers)
		for (const Shape& shape : layer.shapes)
			if (shape.kind == Sig("Rndr"))
				w.Bytes(shape.pieceData);
	auto addStyle = [&](uint32_t style) {
		ShapeStyle resolved = ResolveShapeStyle(styles, style);
		w.UInt32(uint32_t(resolved.fill.color.r) << 24 | uint32_t(resolved.fill.color.g) << 16
			| uint32_t(resolved.fill.color.b) << 8 | (resolved.fill.set ? 1 : 0));
		w.UInt32(uint32_t(resolved.pen.color.r) << 24 | uint32_t(resolved.pen.color.g) << 16
			| uint32_t(resolved.pen.color.b) << 8 | (resolved.pen.set ? 1 : 0));
		w.Float(resolved.penSize);
	};
	for (const ChartSeries& series : chart.series)
		addStyle(series.style);
	for (const ChartAxis& axis : chart.axes)
		addStyle(axis.style);
	return std::string(w.Data().begin(), w.Data().end());
}


bool
RenderChart3D(const ChartPart& chart, const StyleMgr& styles, int32 width, int32 height,
	float normal[3], ChartRender3D& out)
{
	out.bitmap.reset();
	out.labels.clear();
	ChartCamera camera;
	// (CreateGLView: not under 10 either way)
	if (width <= 10 || height <= 10 || !ReadChartCamera(chart, camera))
		return false;
	GLHolder* holder = HolderFor(width, height);
	if (holder == NULL)
		return false;
	TChartGLView* view = holder->view;
	view->LockGL();
	if (!holder->ready) {
		view->SetUp();
		holder->ready = true;
	}
	view->EnforceState();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPushMatrix();
	SnapLightAndCamera(camera);
	glNormal3fv(normal);
	DrawInfo info;
	info.chart = &chart;
	info.styles = &styles;
	info.selected = int32(chart.chartValue8);
	DrawChart(info);
	glGetFloatv(GL_CURRENT_NORMAL, normal);
	AxisLabels(chart, view->WidthOf(), view->HeightOf(), out);
	glFlush();
	view->SwapBuffers();
	glPopMatrix();
	view->UnlockGL();

	BBitmap frame(BRect(0, 0, width - 1, height - 1), B_RGB32);
	if (frame.InitCheck() != B_OK || view->CopyPixelsOut(B_ORIGIN, &frame) != B_OK) {
		out.labels.clear();
		return false;
	}
	// TTransManager::DrawBitmap with B_OP_OVER: the clear color lets
	// through what's under it
	std::unique_ptr<BBitmap> bitmap(new BBitmap(frame.Bounds(), B_RGBA32));
	if (bitmap->InitCheck() != B_OK)
		return false;
	for (int32 y = 0; y < height; y++) {
		const uint8* from = (const uint8*)frame.Bits() + y * frame.BytesPerRow();
		uint8* to = (uint8*)bitmap->Bits() + y * bitmap->BytesPerRow();
		for (int32 x = 0; x < width; x++, from += 4, to += 4) {
			to[0] = from[0];
			to[1] = from[1];
			to[2] = from[2];
			to[3] = from[0] == kClear[2] && from[1] == kClear[1] && from[2] == kClear[0] ? 0 : 255;
		}
	}
	out.bitmap = std::move(bitmap);
	return true;
}


int32
FindChart3DObject(const ChartPart& chart, const StyleMgr& styles, int32 width, int32 height,
	BPoint where, float normal[3])
{
	ChartCamera camera;
	if (width <= 10 || height <= 10 || !ReadChartCamera(chart, camera))
		return -1;
	GLHolder* holder = HolderFor(width, height);
	if (holder == NULL)
		return -1;
	TChartGLView* view = holder->view;
	view->LockGL();
	if (!holder->ready) {
		view->SetUp();
		holder->ready = true;
	}
	glShadeModel(GL_FLAT);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glPushMatrix();
	SnapLightAndCamera(camera);
	DrawInfo info;
	info.chart = &chart;
	info.styles = &styles;
	info.clickTest = true;
	glNormal3fv(normal);
	DrawChart(info);
	glGetFloatv(GL_CURRENT_NORMAL, normal);
	uint8 pixel[3] = {0, 0, 0};
	glReadBuffer(GL_BACK);
	glReadPixels(GLint(where.x), GLint(view->HeightOf() - where.y), 1, 1, GL_RGB,
		GL_UNSIGNED_BYTE, pixel);
	glPopMatrix();
	view->EnforceState();
	view->UnlockGL();
	int32 found = 0xffff - int32(pixel[0] | uint32(pixel[1]) << 8);
	return found == 0xffff ? -1 : found;
}

} // namespace gobe
