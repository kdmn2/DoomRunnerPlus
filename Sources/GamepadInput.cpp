//======================================================================================================================
// Project: DoomRunnerPlus
//----------------------------------------------------------------------------------------------------------------------
// Description: implementation of the gamepad -> Qt key events bridge
//======================================================================================================================

#include "GamepadInput.hpp"

#include "Utils/ErrorHandling.hpp"  // logInfo
#include "Utils/OSUtils.hpp"        // getThisLauncherDataDir

#include <QApplication>
#include <QWidget>
#include <QKeyEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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
	bool btnA = false, btnB = false, btnBack = false, btnLb = false, btnRb = false;
	int axisX = 0, axisY = 0;            // latest left-stick values

	bool dirActive[ 4 ] = {};            // one entry per Ctrl_Up/Down/Left/Right
	int repeatCtrl = -1;                 // control currently being auto-repeated, -1 = none
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
	applyDefaultBindings();

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
	SDL_QuitSubSystem( SDL_INIT_GAMECONTROLLER );
#endif
	delete state_;
}

void GamepadInput::applyDefaultBindings()
{
	bindings[ Ctrl_Up ]        = { .isTabAction = false, .key = Qt::Key_Up,        .tabDelta = 0 };
	bindings[ Ctrl_Down ]      = { .isTabAction = false, .key = Qt::Key_Down,      .tabDelta = 0 };
	bindings[ Ctrl_Left ]      = { .isTabAction = false, .key = Qt::Key_Left,      .tabDelta = 0 };
	bindings[ Ctrl_Right ]     = { .isTabAction = false, .key = Qt::Key_Right,     .tabDelta = 0 };
	bindings[ Ctrl_Activate ]  = { .isTabAction = false, .key = Qt::Key_Return,    .tabDelta = 0 };
	bindings[ Ctrl_Back ]      = { .isTabAction = false, .key = Qt::Key_Escape,    .tabDelta = 0 };
	bindings[ Ctrl_FocusNext ] = { .isTabAction = false, .key = Qt::Key_Tab,       .tabDelta = 0 };
	bindings[ Ctrl_NextTab ]   = { .isTabAction = true,  .key = 0,                 .tabDelta = +1 };
	bindings[ Ctrl_PrevTab ]   = { .isTabAction = true,  .key = 0,                 .tabDelta = -1 };
}

GamepadInput::Binding GamepadInput::parseBinding( const QString & name ) const
{
	const QString n = name.trimmed();

	if (n.compare("NextTab", Qt::CaseInsensitive) == 0)
		return { .isTabAction = true, .key = 0, .tabDelta = +1 };
	if (n.compare("PrevTab", Qt::CaseInsensitive) == 0)
		return { .isTabAction = true, .key = 0, .tabDelta = -1 };

	static const QHash< QString, int > keyNames = {
		{ "up", Qt::Key_Up }, { "down", Qt::Key_Down }, { "left", Qt::Key_Left }, { "right", Qt::Key_Right },
		{ "return", Qt::Key_Return }, { "enter", Qt::Key_Return }, { "escape", Qt::Key_Escape }, { "esc", Qt::Key_Escape },
		{ "tab", Qt::Key_Tab }, { "space", Qt::Key_Space }, { "pageup", Qt::Key_PageUp }, { "pagedown", Qt::Key_PageDown },
		{ "home", Qt::Key_Home }, { "end", Qt::Key_End },
	};

	auto it = keyNames.constFind( n.toLower() );
	if (it == keyNames.cend())
		return {};
	return { .isTabAction = false, .key = it.value(), .tabDelta = 0 };
}

void GamepadInput::loadConfig()
{
	applyDefaultBindings();  // start from defaults, override what the config specifies

	QFile file( os::getThisLauncherDataDir() + "/controller.json" );
	if (!file.open( QIODevice::ReadOnly ))
		return;

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson( file.readAll(), &err );
	if (err.error != QJsonParseError::NoError || !doc.isObject())
	{
		logInfo() << "Gamepad config won't be used - controller.json is invalid:" << err.errorString();
		return;
	}
	const QJsonObject root = doc.object();

	struct NamedBinding { const char * key; int ctrl; };
	static const NamedBinding configMap[] = {
		{ "up", Ctrl_Up }, { "down", Ctrl_Down }, { "left", Ctrl_Left }, { "right", Ctrl_Right },
		{ "activate", Ctrl_Activate }, { "back", Ctrl_Back }, { "focus_next", Ctrl_FocusNext },
		{ "next_tab", Ctrl_NextTab }, { "prev_tab", Ctrl_PrevTab },
	};

	for (const NamedBinding & nb : configMap)
	{
		if (const QJsonValue v = root.value( nb.key ); v.isString())
		{
			Binding b = parseBinding( v.toString() );
			if (b.isTabAction || b.key != 0)
				bindings[ nb.ctrl ] = b;
		}
	}
}

void GamepadInput::start()
{
#ifdef ENABLE_GAMEPAD
	loadConfig();

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
					resetPressedState();
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
						case SDL_CONTROLLER_BUTTON_A:          if (!state_->btnA)    invokeControl( Ctrl_Activate );  state_->btnA = true;    break;
						case SDL_CONTROLLER_BUTTON_B:          if (!state_->btnB)    invokeControl( Ctrl_Back );      state_->btnB = true;    break;
						case SDL_CONTROLLER_BUTTON_BACK:       if (!state_->btnBack) invokeControl( Ctrl_FocusNext ); state_->btnBack = true; break;
						case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: if (!state_->btnRb) invokeControl( Ctrl_NextTab );  state_->btnRb = true;    break;
						case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  if (!state_->btnLb) invokeControl( Ctrl_PrevTab );  state_->btnLb = true;    break;
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
						case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: state_->btnRb = false; break;
						case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  state_->btnLb = false; break;
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

	updateDir( Ctrl_Up,    state_->btnUp    || state_->axisY < -AXIS_DEADZONE );
	updateDir( Ctrl_Down,  state_->btnDown  || state_->axisY >  AXIS_DEADZONE );
	updateDir( Ctrl_Left,  state_->btnLeft  || state_->axisX < -AXIS_DEADZONE );
	updateDir( Ctrl_Right, state_->btnRight || state_->axisX >  AXIS_DEADZONE );
#endif
}

void GamepadInput::invokeControl( int ctrlIdx )
{
	const Binding & b = bindings[ ctrlIdx ];
	if (b.isTabAction)
	{
		if (b.tabDelta > 0)
			emit nextTab();
		else if (b.tabDelta < 0)
			emit prevTab();
	}
	else if (b.key != 0)
	{
		sendKeyClick( b.key );
	}
}

void GamepadInput::updateDir( int ctrlIdx, bool active )
{
#ifdef ENABLE_GAMEPAD
	if (active == state_->dirActive[ ctrlIdx ])
		return;  // no change

	state_->dirActive[ ctrlIdx ] = active;
	const int key = bindings[ ctrlIdx ].key;

	if (active)
	{
		sendKeyPress( key );
		state_->repeatCtrl = ctrlIdx;
		repeatTimer_.start();
	}
	else if (ctrlIdx == state_->repeatCtrl)
	{
		sendKeyRelease( key );
		state_->repeatCtrl = -1;
		repeatTimer_.stop();
	}
#endif
}

void GamepadInput::resetPressedState()
{
	state_->btnUp = state_->btnDown = state_->btnLeft = state_->btnRight = false;
	state_->btnA = state_->btnB = state_->btnBack = state_->btnLb = state_->btnRb = false;
	state_->axisX = state_->axisY = 0;
	state_->repeatCtrl = -1;
	repeatTimer_.stop();
}

void GamepadInput::repeatCurrent()
{
#ifdef ENABLE_GAMEPAD
	if (state_->repeatCtrl >= 0)
		sendKeyPress( bindings[ state_->repeatCtrl ].key );
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
