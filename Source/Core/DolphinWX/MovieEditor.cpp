
#include <wx/wx.h>
#include <wx/dcbuffer.h>

#include "MovieEditor.h"
#include "DolphinWX/Main.h"
#include "Frame.h"
#include "Core/Movie.h"

const int lwidth = 8;
const int lheight = 13;
const int linecount = 20;
wxStaticText* lines[linecount];
std::string linestext[linecount];
bool pressed[linecount][8][12];
const int idstart = 310;
wxStaticText* label;
std::string labeltext;
//wxScrollBar* scrollbar;
int maxlines = 0;
int scroll = 0;

double speed;

bool changed = false;

MovieEditor::MovieEditor(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& position, const wxSize& size, long style) : wxDialog(parent, id, title, position, size, style)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxFont font(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	wxSize fsize(lwidth, lheight);
	font.SetPixelSize(fsize);
	SetFont(font);

	wxSize linesize(GetSize().GetX(), lheight);

	label = new wxStaticText(this, 300, _("0 | 128 128 128 128 000 000 A B X Y Z L R S < ^ > v"), wxPoint(0,0), linesize);
	//scrollbar = new wxScrollBar(this, 301, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
	updateScrollbar();

	//label->Bind(wxEVT_LEFT_DOWN, &MovieEditor::OnEvent_Click, this);
	Bind(wxEVT_CLOSE_WINDOW, &MovieEditor::OnEvent_Close, this);
	Bind(wxEVT_IDLE, &MovieEditor::repaint, this);
	Bind(wxEVT_SIZE, &MovieEditor::resized, this);

	Bind(wxEVT_PAINT, &MovieEditor::paint, this);

	for (int i = 0; i < linecount; i++) {
		lines[i] = new wxStaticText(this, idstart + i, std::to_string(fsize.GetX()) + " " + std::to_string(fsize.GetY()), wxPoint(0,(i+1)*lheight), linesize);
		lines[i]->Wrap(-1);
		lines[i]->Bind(wxEVT_LEFT_DOWN, &MovieEditor::OnEvent_Click, this);
	}
	Show();
	update(0);
}

MovieEditor::~MovieEditor()
{
	main_frame->g_MovieEditor = nullptr;
	//Show(false);
}

void MovieEditor::paint(wxPaintEvent& event) {
	wxPaintDC dc(this);
	dc.Clear();
	//dc.SetBackgroundMode(wxPENSTYLE_SOLID);
	wxColor cBlack(*wxBLACK);
	wxColor cRed(*wxRED);
	wxColor cGrey(0xC0C0C0/*C0MBO BREAKER*/);
	/*good joke*/
		
	wxColor cText = cBlack;

	dc.DrawText(labeltext, 0, 400);
	for (int i = 0; i < linecount; i++) {
		//dc.DrawText(linestext[i], 0, 400 + (i + 1)*lheight);
		int pad = 0;
		int button = 0;
		bool colorInput = false;
		cText = cBlack;
		for (int j = 0; j < linestext[i].length(); j++) {
			if (colorInput) {
				if (pressed[i][pad][button]) {
					cText = cBlack;
				}
				else {
					cText = cGrey;
				}
				button++;
				if (button == 12) {
					colorInput = false;
					pad++;
				}
			}
			else if (linestext[i][j]=='|') {
				colorInput = true;
				button = 0;
				cText = cBlack;
			}
			dc.SetTextForeground(cText);
			dc.DrawText(linestext[i][j], j*lwidth, 400 + (i + 1)*lheight);
		}
	}
	//updateScrollbar();
}

void MovieEditor::resized(wxSizeEvent& event)
{
	Refresh();
}

void MovieEditor::updateScrollbar() {
	SetScrollbar(wxSB_VERTICAL, scroll, linecount, maxlines);
}

void MovieEditor::repaint(wxIdleEvent&)
{
	if (changed)
	{
		label->SetLabel(labeltext);
		for (int i = 0; i < linecount; i++) {
			lines[i]->SetLabel(linestext[i]);
		}
		changed = false;
	}
}

void MovieEditor::update(int mode)
{
	bool loadedState = (mode == 1);
	u64 totalbytes = Movie::GetTotalBytes();
	if (totalbytes==0)
	{
		labeltext="Waiting for movie to start.";
		changed = true;
		Refresh();
		return;
	}
	u8* input = Movie::GetInput();
	u64 curbyte = Movie::GetCurrentByte();
	int pads = Movie::GetControllerNumber();

	int curframe = curbyte / (8 * pads);
	int totalframes = totalbytes / (8 * pads);
	if (totalframes > maxlines) { maxlines = totalframes; }
	int maxscroll = totalframes - (linecount - 1);
	if (maxscroll < 0) { maxscroll = 0; }
	int maxscrollre = curframe - (linecount - 1);
	if (maxscrollre < 0) { maxscrollre = 0; }
	if (Movie::IsRecordingInput()) {
		scroll = maxscrollre;
	}
	else {
		scroll = curframe - linecount / 2;
		if (scroll < 0) { scroll = 0; }
		else if (scroll > maxscroll) { scroll = maxscroll; }
	}

	for (int line = 0; line<linecount; line++)
	{
		int frame = scroll + line;
		int index = frame * (8 * pads);
		std::string text = "B.";
		if (index >= 0 && index <= totalbytes)
		{
			if (index == curbyte) { text += "-->"; }
			else { text += "   "; }
			text += std::to_string(frame);
			if(index < totalbytes){
				for (int i = 0; i < pads; i++) {
					text += "|SABXYZ^v<>LR";
					int loc = index + 8*i;
					updatePressed(input[loc], input[loc + 1],i,line);
					/*
					for (int j = 0; j < 8; j++)
					{
						std::stringstream tmp;
						tmp << std::setfill('0') << std::hex << std::setw(2) << std::uppercase << (int)input[loc + j];
						text += tmp.str() + " ";
					}
					*/
				}
			}
		}
		linestext[line] = text;
	}
	std::string text = "";
	text += std::to_string((int)curframe);
	text += "/";
	text += std::to_string((int)totalframes);
	text += " | ";
	text += std::to_string((int)Movie::GetReadOnly());
	//label->SetLabel(text);
	labeltext = text;
	changed = true;
	Refresh();
}

void MovieEditor::updatePressed(u8 b1, u8 b2, int pad, int line) {
	for (int i = 0; i < 12; i++) {
		if (i < 8) {
			pressed[line][pad][i] = (b1 & 1) != 0;
			b1 >>= 1;
		}
		else {
			pressed[line][pad][i] = (b2 & 1) != 0;
			b2 >>= 1;
		}
	}
}

void MovieEditor::OnEvent_Close(wxCloseEvent&)
{
	Destroy();
}

void MovieEditor::OnEvent_Click(wxMouseEvent& event)
{
	update(0);
	wxStaticText* ob = (wxStaticText*)(event.GetEventObject());
	int id = ob->GetId()-idstart;
	ob->SetLabel(std::to_string(id));
	
	//label->SetLabel(std::to_string(event.GetX())+" "+std::to_string(event.GetY()));
	//Refresh();
}
