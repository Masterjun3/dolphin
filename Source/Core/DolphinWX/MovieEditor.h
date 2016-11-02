
#include <wx/wx.h>

class MovieEditor : public wxDialog
{
public:
	MovieEditor(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Movie Editor"),
		const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(800,800),
		long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	~MovieEditor();

	void update(int);

private:
	void paint(wxPaintEvent&);
	void resized(wxSizeEvent&);
	void repaint(wxIdleEvent&);
	void OnEvent_Close(wxCloseEvent&);
	void OnEvent_Click(wxMouseEvent&);
	void updatePressed(u8, u8, int, int);
	void EnsureLagWatchSize(size_t bound);
};
