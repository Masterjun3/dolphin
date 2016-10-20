
#include <wx/wx.h>
#include <wx/dcbuffer.h>

#include "MovieEditor.h"
#include "DolphinWX/Main.h"
#include "Frame.h"
#include "Core/Movie.h"


MovieEditor::MovieEditor(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& position, const wxSize& size, long style) : wxDialog(parent, id, title, position, size, style)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	label = new wxStaticText(this, 300, _("0 | 128 128 128 128 000 000 A B X Y Z L R S < ^ > v"), wxDefaultPosition, GetSize());

	label->Bind(wxEVT_LEFT_DOWN, &MovieEditor::OnEvent_Click, this);
	Bind(wxEVT_CLOSE_WINDOW, &MovieEditor::OnEvent_Close, this);
	Bind(wxEVT_IDLE, &MovieEditor::repaint, this);
	Show();
}

MovieEditor::~MovieEditor()
{
	main_frame->g_MovieEditor = nullptr;
}

std::string tmp = "temp";
bool changed = false;

void MovieEditor::repaint(wxIdleEvent&)
{
	if (changed)
	{
		changed = false;
		label->SetLabel(tmp);
	}
}

void MovieEditor::update()
{
	if (!Movie::IsMovieActive())
	{
		label->SetLabel("Waiting for movie to start.");
		return;
	}
	std::string text = "";
	u8* input = Movie::GetInput();
	u64 curbyte = Movie::GetCurrentByte();
	u64 totalbytes = Movie::GetTotalBytes();
	int pads = Movie::GetControllerNumber();
	for (int frame = -10; frame<10; frame++)
	{
		int index = curbyte + 8*pads*frame;
		text += "B.";
		if (index > 0 && index < totalbytes)
		{
			if (frame == -1) { text += "-->"; }
			else { text += "   "; }
			text += std::to_string(index);
			for (int i = 0; i < pads; i++) {
				text += "|";
				int loc = index + 8*i;
				for (int j = 0; j < 8; j++)
				{

					std::stringstream tmp;
					tmp << std::hex << (int)input[loc + j];
					text += tmp.str() + " ";
				}
			}
		}
		text += '\n';
	}
	text += '\n';
	text += std::to_string((int)curbyte);
	text += '\n';
	text += std::to_string((int)totalbytes);
	//label->SetLabel(text);
	tmp = text;
	changed = true;
	//Refresh();
}

void MovieEditor::OnEvent_Close(wxCloseEvent&)
{
	Destroy();
}

void MovieEditor::OnEvent_Click(wxMouseEvent& event)
{
	label->SetLabel(std::to_string(event.GetX())+" "+std::to_string(event.GetY()));
	//Refresh();
}