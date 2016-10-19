#include "MovieEditor.h"
#include <wx/wxprec.h>
#include "DolphinWX/Main.h"
#include "Frame.h"


MovieEditor::MovieEditor(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& position, const wxSize& size, long style) : wxDialog(parent, id, title, position, size, style)
{
	Bind(wxEVT_CLOSE_WINDOW, &MovieEditor::OnEvent_Close, this);
	Show();
}

MovieEditor::~MovieEditor()
{
	main_frame->g_MovieEditor = nullptr;
}

void MovieEditor::OnEvent_Close(wxCloseEvent&)
{
	Destroy();
}