
#include <wx/dialog.h>

class MovieEditor : public wxDialog
{
public:
	MovieEditor(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Movie Editor"),
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_DIALOG_STYLE | wxSTAY_ON_TOP);

	~MovieEditor();
	void OnEvent_Close(wxCloseEvent &);
};
