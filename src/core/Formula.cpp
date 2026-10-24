#include "Formula.h"

#include "DocWriter.h"

namespace gobe {

CellID
ReadCellID(DocArchive& a)
{
	CellID id;
	id.a = a.UInt16();
	id.b = a.UInt16();
	return id;
}


CellRef
ReadCellRef(DocArchive& a)
{
	CellRef ref;
	ref.cell = ReadCellID(a);
	ref.flags = a.UInt16();
	return ref;
}


EvalResult
ReadEvalResult(DocArchive& a)
{
	EvalResult r;
	r.type = a.UInt32();
	switch (r.type) {
		case Sig("Err#"):
			r.value = a.UInt32();
			break;
		case Sig("Bool"):
		case Sig("Dbl#"):
			r.number = a.Double();
			break;
		case Sig("Strn"):
			r.text = a.String();
			break;
		case Sig("Time"):
			r.value = a.UInt32();
			break;
	}
	return r;
}


static std::string
ShortString(DocArchive& a)
{
	uint16_t length = a.UInt16();
	std::vector<uint8_t> bytes = a.Bytes(length);
	return std::string(bytes.begin(), bytes.end());
}


std::vector<FormulaToken>
ReadFormula(DocArchive& a)
{
	std::vector<FormulaToken> tokens;
	while (true) {
		FormulaToken t;
		t.kind = a.UInt16();
		switch (t.kind) {
			case 0x11:
				t.strings.push_back(ShortString(a));
				break;
			case 0x12:
				t.value = a.Bool();
				break;
			case 0x13:
			case 0x14:
			case 0x15:
			case 0x16:
				t.number = a.Double();
				break;
			case 0x17:
				t.value = a.UInt16();
				break;
			case 0x18:
				t.value = a.UInt32();
				break;
			case 0x19:
				t.refs.push_back(ReadCellRef(a));
				break;
			case 0x1a:
				t.refs.push_back(ReadCellRef(a));
				t.refs.push_back(ReadCellRef(a));
				break;
			case 0x1b:
				t.value = a.UInt32();
				t.strings.push_back(ShortString(a));
				t.strings.push_back(ShortString(a));
				break;
			case 0x1c:
				t.value = a.UInt8();
				t.strings.push_back(ShortString(a));
				break;
			case 0x1e:	// strings + cell
			case 0x1f:	// strings + range
				t.strings.push_back(ShortString(a));
				t.strings.push_back(ShortString(a));
				t.refs.push_back(ReadCellRef(a));
				if (t.kind == 0x1f)
					t.refs.push_back(ReadCellRef(a));
				break;
			default:
				break;	// operators: no payload
		}
		tokens.push_back(t);
		if (t.kind == 0x1d)
			return tokens;
	}
}


void
ReadExpression(DocArchive& a, Expression& out)
{
	a.CheckSig(Sig("EXPR"));
	out.cell = ReadCellID(a);
	out.text = a.String();
	out.valueC = a.UInt32();
	out.value10 = a.UInt32();
	out.source = a.String();
	out.result = ReadEvalResult(a);
	out.format = a.String();
	if (a.UInt32() != 0)
		out.formula = ReadFormula(a);
	uint32_t links = a.UInt32();
	for (uint32_t i = 0; i < links; i++)
		out.links.push_back(a.UInt32());
	a.CheckSig(Sig("ExpR"));
}



static void
WriteCellRef(DocWriter& w, const CellRef& r)
{
	w.UInt16(r.cell.a);
	w.UInt16(r.cell.b);
	w.UInt16(r.flags);
}


void
WriteFormula(DocWriter& w, const std::vector<FormulaToken>& tokens)
{
	for (const FormulaToken& t : tokens) {
		w.UInt16(t.kind);
		auto str = [&](size_t i) { w.ShortString(i < t.strings.size() ? t.strings[i] : ""); };
		auto ref = [&](size_t i) { WriteCellRef(w, i < t.refs.size() ? t.refs[i] : CellRef()); };
		switch (t.kind) {
			case 0x11:
				str(0);
				break;
			case 0x12:
				w.Bool(t.value != 0);
				break;
			case 0x13:
			case 0x14:
			case 0x15:
			case 0x16:
				w.Double(t.number);
				break;
			case 0x17:
				w.UInt16(uint16_t(t.value));
				break;
			case 0x18:
				w.UInt32(t.value);
				break;
			case 0x19:
				ref(0);
				break;
			case 0x1a:
				ref(0);
				ref(1);
				break;
			case 0x1b:
				w.UInt32(t.value);
				str(0);
				str(1);
				break;
			case 0x1c:
				w.UInt8(uint8_t(t.value));
				str(0);
				break;
			case 0x1e:
			case 0x1f:
				str(0);
				str(1);
				ref(0);
				if (t.kind == 0x1f)
					ref(1);
				break;
			default:
				break;
		}
	}
}

void
WriteEvalResult(DocWriter& w, const EvalResult& r)
{
	w.UInt32(r.type);
	switch (r.type) {
		case Sig("Err#"):
		case Sig("Time"):
			w.UInt32(r.value);
			break;
		case Sig("Bool"):
		case Sig("Dbl#"):
			w.Double(r.number);
			break;
		case Sig("Strn"):
			w.String(r.text);
			break;
	}
}


void
WriteExpression(DocWriter& w, const Expression& e)
{
	w.Sig(Sig("EXPR"));
	w.UInt16(e.cell.a);
	w.UInt16(e.cell.b);
	w.String(e.text);
	w.UInt32(e.valueC);
	w.UInt32(e.value10);
	w.String(e.source);
	WriteEvalResult(w, e.result);
	w.String(e.format);
	w.UInt32(e.formula.empty() ? 0 : 1);
	if (!e.formula.empty())
		WriteFormula(w, e.formula);
	w.UInt32(uint32_t(e.links.size()));
	for (uint32_t link : e.links)
		w.UInt32(link);
	w.Sig(Sig("ExpR"));
}

} // namespace gobe
