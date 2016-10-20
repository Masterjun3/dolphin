
#include <wx/wx.h>

class MovieEditor : public wxDialog
{
public:
	MovieEditor(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Movie Editor"),
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(800,800),
		long style = wxDEFAULT_DIALOG_STYLE);

	~MovieEditor();

	void update();

private:
	void repaint(wxIdleEvent&);
	void OnEvent_Close(wxCloseEvent&);
	void OnEvent_Click(wxMouseEvent&);
	wxStaticText* label;
};
