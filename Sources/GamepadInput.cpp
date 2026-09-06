//======================================================================================================================
// Project: DoomRunnerPlus
//----------------------------------------------------------------------------------------------------------------------
// Description: implementation of the gamepad -> Qt key events bridge
//======================================================================================================================

#include "GamepadInput.hpp"

#include "Utils/ErrorHandling.hpp"  // logInfo

#include <QApplication>
#include <QWidget>
#include <QKeyEvent>

#ifdef ENABLE_GAMEPAD
	#include <SDL.h>
	#include <SDL_gamecontroller.h>
#endif


struct GamepadInput::State
{
#ifdef ENABLE_GAMEPAD
	void * controller = nullptr;         // SDL_GameController*
	SDL_JoystickID controllerId = -1;    // instance id of the opened controller
#endif
	bool btnUp = false, btnDown = false, btnLeft = false, btnRight = false;
	bool btnA = false, btnB = false, btnBack = false;
	int axisX = 0, axisY = 0;            // latest left-stick values

	bool dirActive[4] = {};              // one entry per Dir (Up/Down/Left/Right)
	int repeatKey = 0;                   // arrow key currently being auto-repeated, 0 = none
};


namespace {

constexpr int AXIS_DEADZONE = 8000;  // out of 32767

QWidget * focusTarget()
{
	QWidget * target = QApplication::focusWidget();
	if (target == nullptr)
		target = QApplication::activeWindow();
	return target;
}

} // namespace


GamepadInput::GamepadInput( QObject * parent )
 : QObject( parent )
{
	state_ = new State();

	pollTimer_.setInterval( 15 );
	connect( &pollTimer_, &QTimer::timeout, this, &GamepadInput::poll );

	repeatTimer_.setInterval( 120 );
	connect( &repeatTimer_, &QTimer::timeout, this, &GamepadInput::repeatCurrent );
}

GamepadInput::~GamepadInput()
{
#ifdef ENABLE_GAMEPAD
	if (state_->controller)
		SDL_GameControllerClose( static_cast< SDL_GameController * >( state_->controller ) );
	if (state_)
		SDL_QuitSubSystem( SDL_INIT_GAMECONTROLLER );
#endif
	delete state_;
}

void GamepadInput::start()
{
#ifdef ENABLE_GAMEPAD
	if (SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) != 0)
	{
		logInfo() << "Gamepad input disabled - failed to initialize SDL:" << SDL_GetError();
		return;
	}

	// Open the first connected controller. If none is plugged in yet, a later
	// SDL_CONTROLLERDEVICEADDED event will open it.
	poll();
	pollTimer_.start();
#endif
}

void GamepadInput::poll()
{
#ifdef ENABLE_GAMEPAD
	if (SDL_WasInit( SDL_INIT_GAMECONTROLLER ) == 0)
		return;

	SDL_Event event;
	while (SDL_PollEvent( &event ))
	{
		switch (event.type)
		{
			case SDL_CONTROLLERDEVICEADDED:
				if (state_->controller == nullptr)
				{
					state_->controller = SDL_GameControllerOpen( event.cdevice.which );
					state_->controllerId = ( state_->controller )
						? SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( static_cast< SDL_GameController * >( state_->controller ) ) )
						: -1;
				}
				break;

			case SDL_CONTROLLERDEVICEREMOVED:
				if (state_->controller != nullptr && event.cdevice.which == state_->controllerId)
				{
					SDL_GameControllerClose( static_cast< SDL_GameController * >( state_->controller ) );
					state_->controller = nullptr;
					state_->controllerId = -1;
					state_->btnUp = state_->btnDown = state_->btnLeft = state_->btnRight = false;
					state_->btnA = state_->btnB = state_->btnBack = false;
				}
				break;

			case SDL_CONTROLLERBUTTONDOWN:
				if (state_->controller != nullptr && event.cbutton.which == state_->controllerId)
				{
					switch (event.cbutton.button)
					{
						case SDL_CONTROLLER_BUTTON_DPAD_UP:    state_->btnUp    = true;  break;
						case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  state_->btnDown  = true;  break;
						case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  state_->btnLeft  = true;  break;
						case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: state_->btnRight = true;  break;
						case SDL_CONTROLLER_BUTTON_A:          if (!state_->btnA) sendKeyClick( Qt::Key_Return ); state_->btnA = true;  break;
						case SDL_CONTROLLER_BUTTON_B:          if (!state_->btnB) sendKeyClick( Qt::Key_Escape ); state_->btnB = true;  break;
						case SDL_CONTROLLER_BUTTON_BACK:       if (!state_->btnBack) sendKeyClick( Qt::Key_Tab ); state_->btnBack = true;  break;
						default: break;
					}
				}
				break;

			case SDL_CONTROLLERBUTTONUP:
				if (state_->controller != nullptr && event.cbutton.which == state_->controllerId)
				{
					switch (event.cbutton.button)
					{
						case SDL_CONTROLLER_BUTTON_DPAD_UP:    state_->btnUp    = false; break;
						case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  state_->btnDown  = false; break;
						case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  state_->btnLeft  = false; break;
						case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: state_->btnRight = false; break;
						case SDL_CONTROLLER_BUTTON_A:          state_->btnA     = false; break;
						case SDL_CONTROLLER_BUTTON_B:          state_->btnB     = false; break;
						case SDL_CONTROLLER_BUTTON_BACK:       state_->btnBack  = false; break;
						default: break;
					}
				}
				break;

			case SDL_CONTROLLERAXISMOTION:
				if (state_->controller != nullptr && event.caxis.which == state_->controllerId)
				{
					if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
						state_->axisX = event.caxis.value;
					else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
						state_->axisY = event.caxis.value;
				}
				break;

			default:
				break;
		}
	}

	const bool up    = state_->btnUp    || state_->axisY <  -AXIS_DEADZONE;
	const bool down  = state_->btnDown  || state_->axisY >   AXIS_DEADZONE;
	const bool left  = state_->btnLeft  || state_->axisX <  -AXIS_DEADZONE;
	const bool right = state_->btnRight || state_->axisX >   AXIS_DEADZONE;

	updateDir( Dir::Up,    up );
	updateDir( Dir::Down,  down );
	updateDir( Dir::Left,  left );
	updateDir( Dir::Right, right );
#endif
}

void GamepadInput::repeatCurrent()
{
#ifdef ENABLE_GAMEPAD
	if (state_->repeatKey != 0)
		sendKeyPress( state_->repeatKey );
#endif
}

void GamepadInput::updateDir( Dir dir, bool active )
{
#ifdef ENABLE_GAMEPAD
	const int idx = static_cast< int >( dir );
	if (active == state_->dirActive[ idx ])
		return;  // no change

	state_->dirActive[ idx ] = active;
	const int key = dirKey( dir );

	if (active)
	{
		sendKeyPress( key );
		state_->repeatKey = key;
		repeatTimer_.start();
	}
	else if (key == state_->repeatKey)
	{
		sendKeyRelease( key );
		state_->repeatKey = 0;
		repeatTimer_.stop();
	}
#endif
}

void GamepadInput::sendKeyPress( int key )
{
	QWidget * target = focusTarget();
	if (target == nullptr)
		return;
	QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
	QApplication::sendEvent( target, &press );
}

void GamepadInput::sendKeyRelease( int key )
{
	QWidget * target = focusTarget();
	if (target == nullptr)
		return;
	QKeyEvent release( QEvent::KeyRelease, key, Qt::NoModifier );
	QApplication::sendEvent( target, &release );
}

void GamepadInput::sendKeyClick( int key )
{
	sendKeyPress( key );
	sendKeyRelease( key );
}

int GamepadInput::dirKey( Dir dir )
{
	switch (dir)
	{
		case Dir::Up:    return Qt::Key_Up;
		case Dir::Down:  return Qt::Key_Down;
		case Dir::Left:  return Qt::Key_Left;
		default:         return Qt::Key_Right;
	}
}
