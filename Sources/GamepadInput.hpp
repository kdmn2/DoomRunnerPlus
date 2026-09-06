//======================================================================================================================
// Project: DoomRunnerPlus
//----------------------------------------------------------------------------------------------------------------------
// Description: translates SDL game controller input into Qt key events,
//              so the whole UI can be navigated with a gamepad.
//======================================================================================================================

#ifndef GAMEPAD_INPUT_HPP
#define GAMEPAD_INPUT_HPP

#include <QObject>
#include <QTimer>


/// Bridges a connected gamepad/controller to the Qt UI by sending synthetic
/// key events (arrow keys, Tab, Enter, Escape) to the focused widget, and by
/// emitting signals for navigation actions that are not plain keystrokes
/// (e.g. cycling the main tabs with R1/L1).
///
/// The button-to-action mapping is loaded from a <tt>controller.json</tt> file
/// in the launcher's data directory; if the file is absent the defaults are
/// used. This feature is compiled out entirely when SDL2 is not available at
/// build time.
class GamepadInput : public QObject
{
	Q_OBJECT

public:
	explicit GamepadInput( QObject * parent = nullptr );
	~GamepadInput() override;

	void start();

signals:
	void nextTab();
	void prevTab();

private slots:
	void poll();
	void repeatCurrent();

private:
	// the controller controls that can be remapped in controller.json
	enum Control { Ctrl_Up, Ctrl_Down, Ctrl_Left, Ctrl_Right, Ctrl_Activate, Ctrl_Back, Ctrl_FocusNext, Ctrl_NextTab, Ctrl_PrevTab, Control_Count };

	struct Binding
	{
		bool isTabAction = false;   ///< true when this control navigates the main tabs
		int key = 0;                ///< Qt::Key when !isTabAction
		int tabDelta = 0;           ///< +1 (next tab) / -1 (prev tab) when isTabAction
	};

	void loadConfig();
	Binding parseBinding( const QString & name ) const;
	void applyDefaultBindings();

	void sendKeyPress( int key );
	void sendKeyRelease( int key );
	void sendKeyClick( int key );

	void updateDir( int ctrlIdx, bool active );
	void invokeControl( int ctrlIdx );
	void resetPressedState();

	Binding bindings[ Control_Count ];

	QTimer pollTimer_;
	QTimer repeatTimer_;

	struct State;
	State * state_ = nullptr;

	Q_DISABLE_COPY( GamepadInput )
};

#endif // GAMEPAD_INPUT_HPP
