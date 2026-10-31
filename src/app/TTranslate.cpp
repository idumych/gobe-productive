// The app's side of the Text and RTF translators.
#include "TTranslate.h"

#include <stdio.h>
#include <string.h>

#include <Button.h>
#include <File.h>
#include <fs_attr.h>
#include <FilePanel.h>
#include <Font.h>
#include <Handler.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <TextControl.h>
#include <TextView.h>
#include <UnicodeBlockObjects.h>
#include <UTF8.h>
#include <Window.h>

#include "Document.h"
#include "GobeResources.h"
#include "RTF.h"
#include "SSSylk.h"

namespace gobe {

const FontCatalog&
InstalledFonts()
{
	static FontCatalog sFonts;
	static bool sLoaded = false;
	if (sLoaded)
		return sFonts;
	sLoaded = true;
	int32 families = count_font_families();
	for (int32 i = 0; i < families; i++) {
		font_family family;
		uint32 flags;
		if (get_font_family(i, &family, &flags) != B_OK)
			continue;
		int32 styles = count_font_styles(family);
		for (int32 j = 0; j < styles; j++) {
			font_style style;
			if (get_font_style(family, j, &style, &flags) != B_OK)
				continue;
			BFont font;
			font.SetFamilyAndStyle(family, style);
			FontInfo info;
			info.family = family;
			info.style = style;
			// TFontBE: Shift-JIS for fonts with the katakana block
			info.charset = font.Blocks().Includes(B_KATAKANA_BLOCK) ? 0x80 : 0;
			sFonts.fonts.push_back(info);
		}
	}
	font_family family;
	font_style style;
	be_plain_font->GetFamilyAndStyle(&family, &style);
	sFonts.plainFamily = family;
	sFonts.plainStyle = style;
	return sFonts;
}


static bool
Convert(int conversion, bool fromUTF8, const std::string& in, std::string& out, char substitute)
{
	int32 state = 0;
	int32 sourceLength = int32(in.size());
	int32 length = int32(in.size()) * 4 + 4;
	out.resize(length);
	status_t status = fromUTF8
		? convert_from_utf8(conversion, in.data(), &sourceLength, &out[0], &length, &state,
			substitute)
		: convert_to_utf8(conversion, in.data(), &sourceLength, &out[0], &length, &state,
			substitute);
	if (status != B_OK)
		return false;
	out.resize(length);
	return true;
}


void
InstallTextConverter()
{
	SetTextConverter(Convert);
}


// #pragma mark - StyledEdit attributes

static void
ReadTextAttributes(const char* path, TextAttributes& attributes)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return;
	attr_info info;
	// TTextToBlockStream::GetStyles
	if (node.GetAttrInfo("styles", &info) == B_OK && info.type == B_RAW_TYPE && info.size > 0) {
		std::string data(info.size, '\0');
		if (node.ReadAttr("styles", B_RAW_TYPE, 0, &data[0], info.size) == ssize_t(info.size)) {
			text_run_array* runs = BTextView::UnflattenRunArray(data.data());
			if (runs != NULL) {
				for (int32 i = 0; i < runs->count; i++) {
					TextRun run;
					run.offset = runs->runs[i].offset;
					font_family family;
					font_style style;
					runs->runs[i].font.GetFamilyAndStyle(&family, &style);
					run.family = family;
					run.style = style;
					run.size = runs->runs[i].font.Size();
					rgb_color c = runs->runs[i].color;
					run.color = {c.red, c.green, c.blue, c.alpha};
					attributes.runs.push_back(run);
				}
				BTextView::FreeRunArray(runs);
			}
		}
	}
	int32 alignment;
	if (node.GetAttrInfo("alignment", &info) == B_OK && info.type == B_INT32_TYPE
			&& node.ReadAttr("alignment", B_INT32_TYPE, 0, &alignment, 4) == 4) {
		attributes.hasAlignment = true;
		attributes.alignment = alignment;
	}
}


static void
WriteTextAttributes(const char* path, const TextAttributes& attributes)
{
	// TBlockStreamToText::WriteStyleInformation
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return;
	int32 alignment = attributes.hasAlignment ? attributes.alignment : 0;
	node.RemoveAttr("alignment");
	node.WriteAttr("alignment", B_INT32_TYPE, 0, &alignment, 4);
	if (attributes.runs.empty())
		return;
	text_run_array* runs = BTextView::AllocRunArray(int32(attributes.runs.size()));
	if (runs == NULL)
		return;
	for (size_t i = 0; i < attributes.runs.size(); i++) {
		const TextRun& run = attributes.runs[i];
		text_run& out = runs->runs[i];
		out.offset = run.offset;
		out.font = *be_plain_font;
		out.font.SetFamilyAndStyle(run.family.c_str(),
			run.style.empty() ? NULL : run.style.c_str());
		out.font.SetSize(run.size);
		out.color = make_color(run.color.r, run.color.g, run.color.b, run.color.a);
	}
	int32 size = 0;
	void* flat = BTextView::FlattenRunArray(runs, &size);
	if (flat != NULL) {
		node.RemoveAttr("styles");
		node.WriteAttr("styles", B_RAW_TYPE, 0, flat, size);
		free(flat);
	}
	BTextView::FreeRunArray(runs);
}


// #pragma mark - opening

FileKind
IdentifyFile(const char* path)
{
	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return kUnknownFile;
	std::string head(0x8000, '\0');
	ssize_t length = file.Read(&head[0], head.size());
	head.resize(length > 0 ? size_t(length) : 0);

	char type[B_MIME_TYPE_LENGTH] = "";
	BNodeInfo info(&file);
	info.GetType(type);
	if (strcmp(type, "application/x-vnd.gobe.productive-document") == 0
			|| head.compare(0, 10, "GOBE SCOTT") == 0)
		return kGobeDocument;
	// the roster's best translator: RTF (quality 0.9) before text (at
	// most 0.8)
	if (head.compare(0, 5, "{\\rtf") == 0)
		return kRTFFile;
	if (type[0] == '\0') {
		// a file the registrar hasn't typed yet
		update_mime_info(path, false, true, B_UPDATE_MIME_INFO_NO_FORCE);
		info.GetType(type);
	}
	// the SYLK translator's Identify: its types, or text starting with an ID
	// record (whatever type the registrar gave it)
	if ((strcmp(type, "text/sylk") == 0 || strcmp(type, "text/x-sylk") == 0
			|| head.compare(0, 3, "ID;") == 0) && LooksLikeText(head))
		return kSylkFile;
	if (strncmp(type, "text/", 5) != 0 || !LooksLikeText(head))
		return kUnknownFile;
	return TableLikeness(head) >= 0.5f ? kTableFile : kTextFile;
}


bool
ImportFile(const char* path, FileKind kind, Document& doc, BString& error, BString& warning)
{
	std::string message, notice;
	bool ok = false;
	if (kind == kRTFFile) {
		ok = ImportRTF(path, doc, message, InstalledFonts(), &notice);
	} else if (kind == kSylkFile) {
		ok = ImportSylk(path, doc, message, &notice);
	} else if (kind == kTableFile) {
		// the Text translator's 'TABT', 'COMA', 'SEMI' formats: a spreadsheet
		ok = ImportTable(path, doc, message);
	} else if (kind == kTextFile) {
		TextAttributes attributes;
		ReadTextAttributes(path, attributes);
		ok = ImportText(path, doc, message, &attributes);
	}
	BPath p(path);
	if (!ok) {
		// "Unable to import the file “%s”."
		error.SetToFormat(GobeResources::IndexString(6, 17).String(), p.Leaf());
		return false;
	}
	warning = notice.c_str();
	// TImportTranslation::PrepareDocument: "<name> (converted)"
	BString name(p.Leaf());
	name << GobeResources::IndexString(5, 32);
	doc.name = name.String();
	return true;
}


// #pragma mark - saving

static const uint32 kMsgFormat = 'FRMT';	// SavePanel's format items

static const char*
FormatType(SaveFormat format)
{
	switch (format) {
		case kSavePlainText:
			return "text/plain";
		case kSaveRTF:
			return "text/rtf";
		case kSaveSYLK:
			return "text/sylk";
		case kSaveTabText:
			return "text/tab-separated-values";
		case kSaveCommaText:
			return "text/x-comma-separated-values";
		case kSaveSemicolonText:
			return "text/x-semicolon-separated-values";
		default:
			return NULL;
	}
}


// SavePanel::GetCurSuffix: 2 to 4 characters after a '.' at the end
static BString
CurrentSuffix(const char* name)
{
	int32 length = strlen(name);
	if (length > 3)
		for (int32 i = length - 3; length - i < 6 && i >= 0; i--)
			if (name[i] == '.')
				return BString(name + i + 1);
	return BString();
}


// the format's extension: the first, or the last one of up to 4 letters
static BString
FormatSuffix(SaveFormat format)
{
	BString suffix;
	const char* type = FormatType(format);
	if (type == NULL)
		return suffix;
	BMimeType mime(type);
	BMessage extensions;
	const char* extension;
	if (mime.GetFileExtensions(&extensions) == B_OK
			&& extensions.FindString("extensions", &extension) == B_OK) {
		suffix = extension;
		for (int32 i = 1; extensions.FindString("extensions", i, &extension) == B_OK; i++)
			if (strlen(extension) < 5)
				suffix = extension;
	}
	return suffix;
}


static bool
IsFormatSuffix(SaveFormat format, const BString& suffix)
{
	const char* type = FormatType(format);
	if (type == NULL)
		return false;
	BMimeType mime(type);
	BMessage extensions;
	const char* extension;
	if (mime.GetFileExtensions(&extensions) != B_OK)
		return false;
	for (int32 i = 0; extensions.FindString("extensions", i, &extension) == B_OK; i++)
		if (strcasecmp(extension, suffix.String()) == 0)
			return true;
	return false;
}


// SaveLooper: the format items; the suffix follows the format unless the
// user gave the name a suffix of their own (SavePanel::CheckSuffix)
class FormatHandler : public BHandler {
public:
	FormatHandler(BFilePanel* panel)
		: BHandler("format handler"), fPanel(panel), fFormat(kSaveGobe), fCustom(false) {}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgFormat) {
			BHandler::MessageReceived(message);
			return;
		}
		int32 format = kSaveGobe;
		message->FindInt32("format", &format);
		BTextControl* text = dynamic_cast<BTextControl*>(
			fPanel->Window()->FindView("text view"));
		if (text != NULL && !fCustom) {
			BString suffix = CurrentSuffix(text->Text());
			if (fFormat == kSaveGobe) {
				if (suffix.Length() > 0)
					fCustom = true;
			} else if (suffix.Length() == 0 || !IsFormatSuffix(fFormat, suffix))
				fCustom = true;
		}
		fFormat = SaveFormat(format);
		if (!fCustom)
			FixSuffix(fPanel, fFormat);
	}

private:
	BFilePanel* fPanel;
	SaveFormat fFormat;
	bool fCustom;
};


void
FixSuffix(BFilePanel* panel, SaveFormat format)
{
	// SavePanel::SetSuffix
	BTextControl* text = dynamic_cast<BTextControl*>(panel->Window()->FindView("text view"));
	if (text == NULL)
		return;
	BString name(text->Text());
	BString suffix = CurrentSuffix(name.String());
	if (suffix.Length() > 0)
		name.Truncate(name.Length() - suffix.Length() - 1);
	BString wanted = FormatSuffix(format);
	if (wanted.Length() > 0)
		name << "." << wanted;
	text->SetText(name.String());
}


BMenuField*
AddFormatField(BFilePanel* panel, bool textFormats, bool sheetFormats)
{
	BWindow* window = panel->Window();
	if (!window->Lock())
		return NULL;
	BView* textView = window->FindView("text view");
	BMenuField* field = NULL;
	if (textView != NULL && textView->Parent() != NULL) {
		BView* background = textView->Parent();
		// SavePanel::SavePanel: the window grows by a menu field's height;
		// the file list keeps its size, the name and the buttons move down,
		// the field goes where the name was
		BPopUpMenu* menu = new BPopUpMenu("");
		// "File Format"
		field = new BMenuField(textView->Frame(), "format field",
			GobeResources::IndexString(5, 42).String(), menu,
			B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
		field->SetDivider(field->StringWidth(field->Label()) + 4);
		float height = field->PreferredSize().height;
		if (height < textView->Frame().Height())
			height = textView->Frame().Height();
		const char* names[] = {"PoseView", "VScrollBar", "HScrollBar", "CountVw"};
		BView* fixed[4];
		uint32 modes[4];
		for (int i = 0; i < 4; i++) {
			fixed[i] = window->FindView(names[i]);
			if (fixed[i] != NULL) {
				modes[i] = fixed[i]->ResizingMode();
				fixed[i]->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_TOP);
			}
		}
		BRect where = textView->Frame();
		window->ResizeBy(0, height + 4);
		for (int i = 0; i < 4; i++)
			if (fixed[i] != NULL)
				fixed[i]->SetResizingMode(modes[i]);
		field->MoveTo(where.left, where.top + 2);
		field->ResizeTo(where.Width(), height);
		background->AddChild(field);

		FormatHandler* handler = new FormatHandler(panel);
		window->AddHandler(handler);
		// SavePanel::SetFormats: "Gobe Productive", then the translators'
		// formats sorted by name
		BMessage* native = new BMessage(kMsgFormat);
		native->AddInt32("format", kSaveGobe);
		BMenuItem* item = new BMenuItem(GobeResources::IndexString(5, 43).String(), native);
		item->SetMarked(true);
		menu->AddItem(item);
		if (textFormats) {
			menu->AddSeparatorItem();
			BMessage* text = new BMessage(kMsgFormat);
			text->AddInt32("format", kSavePlainText);
			menu->AddItem(new BMenuItem("Plain Text", text));
			BMessage* rtf = new BMessage(kMsgFormat);
			rtf->AddInt32("format", kSaveRTF);
			menu->AddItem(new BMenuItem("RTF", rtf));
		} else if (sheetFormats) {
			// the SYLK and Text translators' spreadsheet formats, by name
			menu->AddSeparatorItem();
			const struct {
				const char* name;
				SaveFormat format;
			} formats[] = {
				{"Comma-delimited text", kSaveCommaText},
				{"SYLK", kSaveSYLK},
				{"Semicolon-delimited text", kSaveSemicolonText},
				{"Tab-delimited text", kSaveTabText}
			};
			for (const auto& f : formats) {
				BMessage* message = new BMessage(kMsgFormat);
				message->AddInt32("format", f.format);
				menu->AddItem(new BMenuItem(f.name, message));
			}
		}
		menu->SetTargetForItems(handler);
	}
	window->Unlock();
	return field;
}


SaveFormat
ChosenFormat(BMenuField* field)
{
	if (field == NULL)
		return kSaveGobe;
	BMenuItem* item = field->Menu()->FindMarked();
	int32 format = kSaveGobe;
	if (item != NULL && item->Message() != NULL)
		item->Message()->FindInt32("format", &format);
	return SaveFormat(format);
}


bool
ExportFile(const Document& doc, const char* path, SaveFormat format, BString& error)
{
	int main = doc.MainPart();
	size_t part = main >= 0 ? size_t(main) : 0;
	std::string message;
	bool ok = false;
	if (format == kSavePlainText) {
		TextAttributes attributes;
		ok = ExportText(doc, part, path, message, &attributes, &InstalledFonts());
		if (ok)
			WriteTextAttributes(path, attributes);
	} else if (format == kSaveRTF) {
		ok = ExportRTF(doc, part, path, message, InstalledFonts());
	} else if (format == kSaveSYLK) {
		ok = ExportSylk(doc, part, path, message);
	} else if (format == kSaveTabText || format == kSaveCommaText
			|| format == kSaveSemicolonText) {
		ok = ExportTable(doc, part, path, format == kSaveTabText ? '\t'
			: format == kSaveCommaText ? ',' : ';', message);
	}
	if (!ok) {
		// "Error writing file!"
		error = GobeResources::IndexString(6, 7);
		return false;
	}
	// the translator's output type (TBlockStreamToText, TExportRTF)
	BNode node(path);
	BNodeInfo info(&node);
	info.SetType(FormatType(format));
	return true;
}

} // namespace gobe
