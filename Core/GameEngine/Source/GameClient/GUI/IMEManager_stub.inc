// Non-Windows IME stub. CJK composition via platform IME is not implemented
// on macOS/Linux in this build; the stub reports "not composing" and has no
// candidates, so text entry falls through to the normal Unicode path.

class IMEManager : public IMEManagerInterface
{
public:
	IMEManager() : m_window(nullptr) {}
	virtual ~IMEManager() override {}

	virtual void init() override {}
	virtual void reset() override {}
	virtual void update() override {}

	virtual void attach(GameWindow *window) override { m_window = window; }
	virtual void detach() override { m_window = nullptr; }
	virtual void enable() override {}
	virtual void disable() override {}
	virtual Bool isEnabled() override { return FALSE; }
	virtual Bool isAttachedTo(GameWindow *window) override { return m_window == window; }
	virtual GameWindow* getWindow() override { return m_window; }
	virtual Bool isComposing() override { return FALSE; }
	virtual void getCompositionString(UnicodeString &string) override { string = UnicodeString::TheEmptyString; }
	virtual Int getCompositionCursorPosition() override { return 0; }
	virtual Int getIndexBase() override { return 1; }

	virtual Int getCandidateCount() override { return 0; }
	virtual const UnicodeString* getCandidate(Int /*index*/) override { return nullptr; }
	virtual Int getSelectedCandidateIndex() override { return 0; }
	virtual Int getCandidatePageSize() override { return 0; }
	virtual Int getCandidatePageStart() override { return 0; }

	virtual Bool serviceIMEMessage(void* /*windowsHandle*/, UnsignedInt /*message*/,
	                               Int /*wParam*/, Int /*lParam*/) override { return FALSE; }
	virtual Int result() override { return 0; }

private:
	GameWindow *m_window;
};

IMEManagerInterface *TheIMEManager = nullptr;

IMEManagerInterface *CreateIMEManagerInterface()
{
	return NEW IMEManager;
}
