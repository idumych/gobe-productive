#include "Pens.h"

#include <algorithm>

#include "DocArchive.h"
#include "DocWriter.h"

namespace gobe {

namespace {

const uint32_t kPenKinds[4] = {Sig("NORM"), Sig("PDOT"), Sig("DASH"), Sig("HASH")};

} // namespace


bool
SamePen(const PenType& a, const PenType& b)
{
	if (a.name != b.name || a.print != b.print || a.kind != b.kind)
		return false;
	switch (a.kind) {
		case 1:
			return a.gap == b.gap && a.filled == b.filled;
		case 2:
			return a.dash == b.dash && a.gap == b.gap;
		case 3:
			return a.gap == b.gap;
		case -1:
			return a.unknownKind == b.unknownKind && a.unknownData == b.unknownData;
	}
	return true;
}


bool
ParsePenResource(const std::vector<uint8_t>& data, PenType& pen)
{
	auto u32 = [&](size_t at) {
		return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16
			| uint32_t(data[at + 3]) << 24;
	};
	if (data.size() < 5)
		return false;
	pen = PenType();
	pen.kind = int(u32(0));
	if (pen.kind < 0 || pen.kind > 3)
		return false;
	size_t at = 4;
	while (at < data.size() && data[at] != 0)
		pen.name += char(data[at++]);
	at++;
	if (at + 4 > data.size())
		return false;
	pen.print = u32(at) != 0;
	at += 4;
	auto next = [&](float& value) {
		if (at + 4 > data.size())
			return false;
		value = float(u32(at));
		at += 4;
		return true;
	};
	float filled = 0;
	if (pen.kind == 2)
		return next(pen.dash) && next(pen.gap);
	if (pen.kind == 1) {
		if (!next(pen.gap) || !next(filled))
			return false;
		pen.filled = filled != 0;
	}
	if (pen.kind == 3)
		return next(pen.gap);
	return true;
}


void
WritePen(DocWriter& w, const PenType& pen)
{
	if (pen.kind < 0) {
		w.UInt32(pen.unknownKind);
		w.UInt32(uint32_t(pen.unknownData.size()));
		w.Bytes(pen.unknownData);
		return;
	}
	DocWriter data;
	data.String(pen.name);
	data.Bool(pen.print);
	if (pen.kind == 2) {
		data.Float(pen.dash);
		data.Float(pen.gap);
	} else if (pen.kind == 1) {
		data.Float(pen.gap);
		data.Bool(pen.filled);
	} else if (pen.kind == 3)
		data.Float(pen.gap);
	w.UInt32(kPenKinds[std::min(3, pen.kind)]);
	w.UInt32(uint32_t(data.Data().size()));
	w.Bytes(data.Data());
}


void
ReadPen(DocArchive& a, PenType& pen)
{
	pen = PenType();
	uint32_t kind = a.UInt32();
	uint32_t size = a.UInt32();
	int index = int(std::find(kPenKinds, kPenKinds + 4, kind) - kPenKinds);
	if (index > 3) {
		// (TUnknownPen: kept as it was; its name, if it has one first)
		pen.kind = -1;
		pen.unknownKind = kind;
		pen.unknownData = a.Bytes(size);
		DocArchive data(pen.unknownData);
		try {
			pen.name = data.String();
			pen.print = data.Bool();
		} catch (const ArchiveError&) {
		}
		return;
	}
	pen.kind = index;
	pen.name = a.String();
	pen.print = a.Bool();
	if (index == 2) {
		pen.dash = a.Float();
		pen.gap = a.Float();
	} else if (index == 1) {
		pen.gap = a.Float();
		pen.filled = a.Bool();
	} else if (index == 3)
		pen.gap = a.Float();
}


Property
PenTypeProperty(const PenType& pen)
{
	DocWriter w;
	w.UInt32(0xffffffff);
	WritePen(w, pen);
	return Property{Sig("PEN "), w.Data()};
}


bool
ReadPenType(const Property& property, PenType& pen)
{
	if (property.type != Sig("PEN "))
		return false;
	DocArchive a(property.data);
	try {
		pen = PenType();
		if (a.UInt32() == 0)
			return true;		// (no pen: a normal one)
		ReadPen(a, pen);
	} catch (const ArchiveError&) {
		return false;
	}
	return true;
}


void
ReadPenMgr(DocArchive& a, PenMgr& pens)
{
	pens = PenMgr();
	a.CheckSig(Sig("PENM"));
	a.UInt32();
	pens.shared = a.Bool();
	pens.own = a.Bool();
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		PenType pen;
		ReadPen(a, pen);
		pens.pens.push_back(pen);
	}
	a.CheckSig(Sig("PenM"));
}


std::vector<uint8_t>
WritePenMgr(const PenMgr& pens)
{
	DocWriter body;
	body.Bool(pens.shared);
	body.Bool(pens.own);
	body.UInt32(uint32_t(pens.pens.size()));
	for (const PenType& pen : pens.pens)
		WritePen(body, pen);
	DocWriter w;
	w.UInt32(Sig("PENM"));
	w.UInt32(uint32_t(body.Data().size()));
	w.Bytes(body.Data());
	w.UInt32(Sig("PenM"));
	return w.Data();
}


const std::vector<PenType>&
DocumentPens(const PenMgr& pens, const std::vector<PenType>& app)
{
	return pens.shared && !pens.own ? app : pens.pens;
}


namespace {

// copyAppPens: before a document's first change
bool
OwnPens(PenMgr& pens, const std::vector<PenType>& app)
{
	if (!pens.shared)
		return false;
	if (!pens.own) {
		pens.pens = app;
		pens.own = true;
	}
	return true;
}

} // namespace


std::string
NewPenName(const std::vector<PenType>& pens, const std::string& name)
{
	for (int n = 1;; n++) {
		std::string candidate = name + std::to_string(n);
		if (std::none_of(pens.begin(), pens.end(),
				[&](const PenType& p) { return p.name == candidate; }))
			return candidate;
	}
}


void
AddPen(PenMgr& pens, const std::vector<PenType>& app, PenType pen)
{
	if (!OwnPens(pens, app))
		return;
	if (std::any_of(pens.pens.begin(), pens.pens.end(),
			[&](const PenType& p) { return SamePen(p, pen); }))
		pen.name = NewPenName(pens.pens, pen.name);
	pens.pens.push_back(pen);
}


bool
ReplacePen(PenMgr& pens, const std::vector<PenType>& app, size_t index, const PenType& pen)
{
	if (!OwnPens(pens, app) || index >= pens.pens.size())
		return false;
	pens.pens[index] = pen;
	return true;
}


bool
DeletePen(PenMgr& pens, const std::vector<PenType>& app, size_t index)
{
	if (!OwnPens(pens, app) || index >= pens.pens.size())
		return false;
	pens.pens.erase(pens.pens.begin() + long(index));
	return true;
}

} // namespace gobe
