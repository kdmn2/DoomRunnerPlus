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
/// key events (arrow keys, Tab, Enter, Escape) to the focused widget.
///
/// This is used to navigate the launcher with a controller. It is compiled out
/// entirely when SDL2 is not available at build time, so other platforms still
/// build without any extra dependency.
class GamepadInput : public QObject
{
	Q_OBJECT

public:
	explicit GamepadInput( QObject * parent = nullptr );
	~GamepadInput() override;

	void start();

private slots:
	void poll();
	void repeatCurrent();

private:
	enum class Dir { Up, Down, Left, Right };

	void sendKeyPress( int key );
	void sendKeyRelease( int key );
	void sendKeyClick( int key );

	void updateDir( Dir dir, bool active );

	static int dirKey( Dir dir );

	QTimer pollTimer_;
	QTimer repeatTimer_;

	struct State;
	State * state_ = nullptr;

	Q_DISABLE_COPY( GamepadInput )
};

#endif // GAMEPAD_INPUT_HPP
