
#include <wx/wx.h>

#include "MovieEditor.h"
#include "DolphinWX/Main.h"
#include "Frame.h"
#include "Core/Movie.h"


MovieEditor::MovieEditor(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& position, const wxSize& size, long style) : wxDialog(parent, id, title, position, size, style)
{
	SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	label = new wxStaticText(this, 300, _("0 | 128 128 128 128 000 000 A B X Y Z L R S < ^ > v"), wxDefaultPosition, GetSize());

	label->Bind(wxEVT_LEFT_DOWN, &MovieEditor::OnEvent_Click, this);
	Bind(wxEVT_CLOSE_WINDOW, &MovieEditor::OnEvent_Close, this);
	Show();
}

MovieEditor::~MovieEditor()
{
	main_frame->g_MovieEditor = nullptr;
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
	for (int frame = -20; frame<0; frame++)
	{
		int index = curbyte + frame * (8 * pads);
		if (index > 0)
		{
			for (int i = 0; i < pads; i++) {
				text += "|";
				for (int j = 0; j < 8; j++)
				{

					std::stringstream tmp;
					tmp << std::hex << (int)input[curbyte + 8 * (pads*frame + i) + j];
					text += tmp.str() + " ";
				}
			}
			text += '\n';
		}
	}
	text += '\n';
	text += std::to_string((int)pads);
	text += '\n';
	text += std::to_string((int)totalbytes);
	//label->SetLabel(text);
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