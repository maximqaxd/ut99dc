#include <string.h>
#include <ctype.h>
#include <kos.h>
#include "GL/glkos.h"

#include "KOSDrv.h"
#include "UnRender.h"
#include "UnCon.h"

IMPLEMENT_CLASS( UKOSViewport );

/*-----------------------------------------------------------------------------
	UKOSViewport implementation.
-----------------------------------------------------------------------------*/

// Game mapping: emit IK_Joy* + IK_JoyPov* so bindings in `User.ini` work.
// Start is mapped to IK_Escape to match standard UT binding (Escape=ShowMenu).
const BYTE UKOSViewport::JoyBtnMapGame[MAX_JOY_BTNS] =
{
	/* CONT_C           */ IK_Joy5,
	/* CONT_B           */ IK_Joy2,
	/* CONT_A           */ IK_Joy1,
	/* CONT_START       */ IK_Escape,
	/* CONT_DPAD_UP     */ IK_JoyPovUp,
	/* CONT_DPAD_DOWN   */ IK_JoyPovDown,
	/* CONT_DPAD_LEFT   */ IK_JoyPovLeft,
	/* CONT_DPAD_RIGHT  */ IK_JoyPovRight,
	/* CONT_Z           */ IK_Joy6,
	/* CONT_Y           */ IK_Joy4,
	/* CONT_X           */ IK_Joy3,
	/* CONT_D           */ IK_Joy8,
	/* CONT_DPAD2_UP    */ IK_Joy9,
	/* CONT_DPAD2_DOWN  */ IK_Joy10,
	/* CONT_DPAD2_LEFT  */ IK_Joy11,
	/* CONT_DPAD2_RIGHT */ IK_Joy12,
};

// UI mapping: menus are driven by IK_Up/Down/Left/Right/Enter/Escape.
const BYTE UKOSViewport::JoyBtnMapUI[MAX_JOY_BTNS] =
{
	/* CONT_C           */ IK_None,
	/* CONT_B           */ IK_Space,     // hack: allow Space=... bindings (e.g. quick "open ...") to work in UWindow/menu
	/* CONT_A           */ IK_Enter,     // keyboard-style activate (opens submenus reliably)
	/* CONT_START       */ IK_Escape,
	/* CONT_DPAD_UP     */ IK_Up,
	/* CONT_DPAD_DOWN   */ IK_Down,
	/* CONT_DPAD_LEFT   */ IK_Left,
	/* CONT_DPAD_RIGHT  */ IK_Right,
	/* CONT_Z           */ IK_None,
	/* CONT_Y           */ IK_Y,         // Yes in UT dialogs (UWindow also recognizes Y/N in many prompts)
	/* CONT_X           */ IK_N,         // No in UT dialogs
	/* CONT_D           */ IK_None,
	/* CONT_DPAD2_UP    */ IK_Up,
	/* CONT_DPAD2_DOWN  */ IK_Down,
	/* CONT_DPAD2_LEFT  */ IK_Left,
	/* CONT_DPAD2_RIGHT */ IK_Right,
};

BYTE UKOSViewport::KeyMap[KBD_MAX_KEYS];

//
// Scancode -> EInputKey translation map.
//
void UKOSViewport::InitKeyMap()
{
	#define INIT_KEY_RANGE( AStart, AEnd, BStart, BEnd ) \
		for( DWORD Key = AStart; Key <= AEnd; ++Key ) KeyMap[Key] = BStart + ( Key - AStart )

	appMemset( KeyMap, 0, sizeof( KeyMap ) );

	INIT_KEY_RANGE( 0x04, 0x1d, IK_A, IK_Z );
	INIT_KEY_RANGE( 0x1e, 0x26, IK_1, IK_9 );
	INIT_KEY_RANGE( 0x3a, 0x45, IK_F1, IK_F12 );

	KeyMap[0x27] = IK_0;
	KeyMap[0x28] = IK_Enter;
	KeyMap[0x29] = IK_Escape;
	KeyMap[0x2a] = IK_Backspace;
	KeyMap[0x2b] = IK_Tab;
	KeyMap[0x2c] = IK_Space;
	KeyMap[0x35] = IK_Tilde;
	KeyMap[0x4f] = IK_Right;
	KeyMap[0x50] = IK_Left;
	KeyMap[0x51] = IK_Down;
	KeyMap[0x52] = IK_Up;

	#undef INIT_KEY_RANGE
}

//
// Static init.
//
void UKOSViewport::InternalClassInitializer( UClass* Class )
{
	guard(UKOSViewport::InternalClassInitializer);

	InitKeyMap();

	unguard;
}

//
// Constructor.
//
UKOSViewport::UKOSViewport( ULevel* InLevel, UKOSClient* InClient )
:	UViewport()
,	Client( InClient )
{
	guard(UKOSViewport::UKOSViewport);

	ColorBytes = 2;
	Caps = 0;

	// Init input.
	if( GIsEditor )
		Input->Init( this );

	Destroyed = false;
	QuitRequested = false;
	HoldCount = 0;
	JoyState = 0;
	MenuNavTimerX = 0.0f;
	MenuNavTimerY = 0.0f;
	MenuNavDirX = 0;
	MenuNavDirY = 0;
	bWasInMenu = 0;
	LastConsoleState = NAME_None;
	InputUpdateTime = 0.0f;

	unguard;
}

// UObject interface.
void UKOSViewport::Destroy()
{
	guard(UKOSViewport::Destroy);

	if( Client->FullscreenViewport == this )
	{
		Client->FullscreenViewport = NULL;
	}
	UViewport::Destroy();

	unguard;
}

//
// Set the mouse cursor according to Unreal or UnrealEd's mode, or to
// an hourglass if a slow task is active. Not implemented.
//
void UKOSViewport::SetModeCursor()
{
	guard(UKOSViewport::SetModeCursor);
	unguard;
}

//
// Update user viewport interface (window title, etc). Not used on Dreamcast.
//
void UKOSViewport::UpdateWindowFrame()
{
	guard(UKOSViewport::UpdateWindowFrame);
	unguard;
}

//
// Open a viewport window.
//
void UKOSViewport::OpenWindow( DWORD InParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(UKOSViewport::OpenWindow);
	check(Actor);
	check(HoldCount == 0);
	UBOOL DoRepaint=0, DoSetActive=0;
	UBOOL NoHard=ParseParam( appCmdLine(), "nohard" );
	NewX = Align(NewX,4);

	// User window of launcher if no parent window was specified.
	if( !InParentWindow )
	{
		QWORD ParentPtr;
		Parse( appCmdLine(), "HWND=", ParentPtr );
		InParentWindow = (DWORD)ParentPtr;
	}

	vid_set_mode( DM_640x480, PM_RGB565 );
	NewX = 640;
	NewY = 480;
		
	SizeX = NewX;
	SizeY = NewY;

	if( !RenDev && Temporary )
		Client->TryRenderDevice( this, "SoftDrv.SoftwareRenderDevice", 0 );
	if( !RenDev && !GIsEditor && !NoHard )
		Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", Client->StartupFullscreen );
	if( !RenDev )
		Client->TryRenderDevice( this, "ini:Engine.Engine.WindowedRenderDevice", 0 );
	check(RenDev);

	if( !Temporary )
		UpdateWindowFrame();
	if( DoRepaint )
		Repaint( 1 );

	unguard;
}

//
// Close a viewport window.  Assumes that the viewport has been opened with
// OpenViewportWindow.  Does not affect the viewport's object, only the
// platform-specific information associated with it.
//
void UKOSViewport::CloseWindow()
{
	guard(UKOSViewport::CloseWindow);


	unguard;
}

//
// Lock the viewport window and set the approprite Screen and RealScreen fields
// of Viewport.  Returns 1 if locked successfully, 0 if failed.  Note that a
// lock failing is not a critical error; it's a sign that a DirectDraw mode
// has ended or the user has closed a viewport window.
//
UBOOL UKOSViewport::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
	guard(UKOSViewport::LockWindow);

	// Success.
	return UViewport::Lock( FlashScale, FlashFog, ScreenClear, RenderLockFlags, HitData, HitSize );

	unguard;
}

//
// Unlock the viewport window.  If Blit=1, blits the viewport's frame buffer.
//
void UKOSViewport::Unlock( UBOOL Blit )
{
	guard(UKOSViewport::Unlock);

	// Unlock base.
	UViewport::Unlock( Blit );

	unguard;
}

//
// Repaint the viewport.
//
void UKOSViewport::Repaint( UBOOL Blit )
{
	guard(UKOSViewport::Repaint);
	if( HoldCount == 0 && RenDev && SizeX && SizeY )
		Client->Engine->Draw( this, Blit );
	unguard;
}

//
// Query fullscreen state. On Dreamcast, we treat the fullscreen viewport as active.
//
UBOOL UKOSViewport::IsFullscreen()
{
	guard(UKOSViewport::IsFullscreen);
	return Client && Client->FullscreenViewport == this;
	unguard;
}

//
// Resize the viewport. On Dreamcast this just updates the client size / fullscreen flag.
//
UBOOL UKOSViewport::ResizeViewport( DWORD NewBlitFlags, INT InNewX, INT InNewY, INT InNewColorBytes )
{
	guard(UKOSViewport::ResizeViewport);

	INT NewX          = (InNewX         == INDEX_NONE) ? SizeX      : InNewX;
	INT NewY          = (InNewY         == INDEX_NONE) ? SizeY      : InNewY;
	INT NewColorBytes = (InNewColorBytes== INDEX_NONE) ? ColorBytes : InNewColorBytes;

	if( NewBlitFlags & BLIT_Fullscreen )
	{
		MakeFullscreen( NewX, NewY, 1 );
	}
	else
	{
		SetClientSize( NewX, NewY, 1 );
		EndFullscreen();
	}
	ColorBytes = NewColorBytes;

	return 1;
	unguard;
}

//
// Set the client size (viewport view size) of a viewport.
//
void UKOSViewport::SetClientSize( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UKOSViewport::SetClientSize);


	SizeX = NewX;
	SizeY = NewY;

	// Optionally save this size in the profile.
	if( UpdateProfile )
	{
		Client->FullscreenViewportX = NewX;
		Client->FullscreenViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

//
// Return the viewport's window.
//
void* UKOSViewport::GetWindow()
{
	return (void*)-1;
}

//
// Try to make this viewport fullscreen, matching the fullscreen
// mode of the nearest x-size to the current window. If already in
// fullscreen, returns to non-fullscreen.
//
void UKOSViewport::MakeFullscreen( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UKOSViewport::MakeFullscreen);

	// If someone else is fullscreen, stop them.
	if( Client->FullscreenViewport )
		Client->EndFullscreen();

	// Save this window.
	SavedX = SizeX;
	SavedY = SizeY;

	// Fullscreen rendering. For now no borderless.
	Client->FullscreenViewport = this;
	SetClientSize( NewX, NewY, false );

	if( UpdateProfile )
	{
		Client->FullscreenViewportX = NewX;
		Client->FullscreenViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

//
//
//
void UKOSViewport::EndFullscreen()
{
	guard(UKOSViewport::EndFullscreen);

	SetClientSize( SavedX, SavedY, false );

	unguard;
}

//
// Update input for viewport.
//
void UKOSViewport::UpdateInput( UBOOL Reset )
{
	guard(UKOSViewport::UpdateInput);


	unguard;
}

//
// If the cursor is currently being captured, stop capturing, clipping, and 
// hiding it, and move its position back to where it was when it was initially
// captured.
//
void UKOSViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL OnlyFocus )
{
	guard(UKOSViewport::SetMouseCapture);

	unguard;
}

UBOOL UKOSViewport::CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta )
{
	guard(UKOSViewport::CauseInputEvent);

	// Route to engine if a valid key
	if( iKey > 0 )
		return Client->Engine->InputEvent( this, (EInputKey)iKey, Action, Delta );
	else
		return 0;

	unguard;
}

//
// Helper function to process joystick axis input with deadzone and scaling.
//
UBOOL UKOSViewport::JoystickAxisEvent( FLOAT RawValue, FLOAT Center, FLOAT Range, EInputKey Key, FLOAT Scale, UBOOL DeadZone )
{
	guard(UKOSViewport::JoystickAxisEvent);

	FLOAT Delta = (RawValue - Center) / Range; // normalize to roughly [-1..1]

	if( DeadZone )
	{
		// 20% deadzone
		const FLOAT DeadZoneValue = 0.2f;
		if( Delta > DeadZoneValue )
			Delta = (Delta - DeadZoneValue) / (1.0f - DeadZoneValue);
		else if( Delta < -DeadZoneValue )
			Delta = (Delta + DeadZoneValue) / (1.0f - DeadZoneValue);
		else
			Delta = 0.0f;
	}

	return CauseInputEvent( Key, IST_Axis, Scale * Delta );

	unguard;
}

void UKOSViewport::TickJoystick( maple_device_t* Dev, const FLOAT DeltaTime )
{
	cont_state_t* State = (cont_state_t*)maple_dev_status( Dev );
	if( !State )
		return;

	// Menu state: use Console state as primary source of truth.
	// Rationale: When the Console consumes key events (Menuing/MenuTyping/etc), the Input system may
	// not reflect key-down state; also some UI paths can desync bShowMenu from actual menu handling.
	static const FName NAME_Menuing     ( TEXT("Menuing") );
	static const FName NAME_EndMenuing  ( TEXT("EndMenuing") );
	static const FName NAME_MenuTyping  ( TEXT("MenuTyping") );
	static const FName NAME_KeyMenuing  ( TEXT("KeyMenuing") );
	static const FName NAME_UWindow     ( TEXT("UWindow") ); // UTMenu.UTConsole frontend / UWindow system

	FName ConsoleState = NAME_None;
	if( Console && Console->GetStateFrame() && Console->GetStateFrame()->StateNode )
		ConsoleState = Console->GetStateFrame()->StateNode->GetFName();

	const UBOOL bConsoleInMenu =
		( ConsoleState == NAME_Menuing )
	||	( ConsoleState == NAME_EndMenuing )
	||	( ConsoleState == NAME_MenuTyping )
	||	( ConsoleState == NAME_KeyMenuing );
	const UBOOL bConsoleInUWindow =
		( ConsoleState == NAME_UWindow );

	const UBOOL bInMenu =
		bConsoleInMenu
	||	bConsoleInUWindow
	||	( Actor && Actor->bShowMenu )
	||	( Actor && Actor->myHUD && Actor->myHUD->MainMenu );
	const BYTE* BtnMap = bInMenu ? JoyBtnMapUI : JoyBtnMapGame;

	auto PulseKey = [&](EInputKey K)
	{
		// Debug: show what we're trying to send in menus (navigation is currently broken).
		if( bInMenu && (K==IK_Up || K==IK_Down || K==IK_Left || K==IK_Right || K==IK_Enter || K==IK_Escape) )
			debugf( TEXT("KOSDrv: Menu PulseKey %d (HUD=%p MainMenu=%p)"), (INT)K, Actor ? Actor->myHUD : NULL, (Actor && Actor->myHUD) ? Actor->myHUD->MainMenu : NULL );
		CauseInputEvent( K, IST_Press );
		CauseInputEvent( K, IST_Release );
	};

	// Buttons: maintain our own button state edges.
	// (In menu, Console can consume IST_Press events without updating Input->KeyDownTable,
	// so relying on Input->KeyDown breaks edge detection and repeats.)
	const DWORD Buttons = State->buttons;
	const DWORD PrevButtons = JoyState;
	const DWORD ChangedButtons = Buttons ^ PrevButtons;

	// HACK: Allow "Space=..." bindings (e.g. Space=open ...) to work in UTMenu's UWindow frontend
	// even before menus are "opened" via ShowMenu. UWindow can consume the key before Input binds fire,
	// so execute the binding directly on B press.
	if( bConsoleInUWindow && (ChangedButtons & CONT_B) && (Buttons & CONT_B) && Input && Input->Bindings[IK_Space].Len() )
	{
		debugf( TEXT("KOSDrv: UWindow B->Space exec '%s'"), *Input->Bindings[IK_Space] );
		Exec( *Input->Bindings[IK_Space], *GLog );
	}

	// On menu enter / menu-state change: reset repeat timers and center cursor.
	if( bInMenu )
	{
		const UBOOL bStateChanged = (ConsoleState != LastConsoleState);
		if( !bWasInMenu || bStateChanged )
		{
			MenuNavTimerX = 0.0f;
			MenuNavTimerY = 0.0f;
			MenuNavDirX = 0;
			MenuNavDirY = 0;

			bShowWindowsMouse = 1;
			WindowsMouseX = SizeX * 0.5f;
			WindowsMouseY = SizeY * 0.5f;
			Client->Engine->MousePosition( this, 0, WindowsMouseX, WindowsMouseY );
		}
	}
	bWasInMenu = bInMenu;
	LastConsoleState = ConsoleState;

	for( DWORD Bit = 0; Bit < MAX_JOY_BTNS; ++Bit )
	{
		const DWORD Mask = ( 1U << Bit );
		const BYTE Key = BtnMap[Bit];
		if( !Key )
			continue;

		const UBOOL bPressed = ( Buttons & Mask ) != 0;
		const UBOOL bWasPressed = ( PrevButtons & Mask ) != 0;

		if( bInMenu )
		{
			if( (Key == IK_Up) || (Key == IK_Down) || (Key == IK_Left) || (Key == IK_Right) )
			{
				if( bPressed && !bWasPressed )
					PulseKey( (EInputKey)Key );
			}
			else if( (Key == IK_Enter) || (Key == IK_Escape) )
			{
				if( bPressed && !bWasPressed )
					PulseKey( (EInputKey)Key );
			}
			else if( (Key != IK_Up) && (Key != IK_Down) && (Key != IK_Left) && (Key != IK_Right) )
			{
				// Other menu keys (if any) - edge pulse.
				if( bPressed && !bWasPressed )
					PulseKey( (EInputKey)Key );
			}
		}
		else
		{
			// Gameplay buttons: hold semantics (press on edge down, release on edge up).
			if( bPressed && !bWasPressed )
				CauseInputEvent( Key, IST_Press );
			else if( !bPressed && bWasPressed )
				CauseInputEvent( Key, IST_Release );
		}
	}

	// Store for next frame.
	JoyState = Buttons;

	// Reset sequence
	if( ( JoyState & CONT_RESET_BUTTONS ) == CONT_RESET_BUTTONS )
		QuitRequested = true;

	// Axes: stick (joyx/joyy) and triggers (ltrig/rtrig)
	JoystickAxisEvent( (FLOAT)State->joyx, 0.0f, 128.0f, IK_JoyX, Client->ScaleXYZ, Client->DeadZoneXYZ > 0.0f );
	JoystickAxisEvent( (FLOAT)State->joyy, 0.0f, 128.0f, IK_JoyY, Client->ScaleXYZ * (Client->InvertY ? 1.0f : -1.0f), Client->DeadZoneXYZ > 0.0f );
	// Triggers are used as buttons in our control scheme (Fire/Jump).
	// Emit Press/Release based on a threshold.
	{
		const INT TriggerThresh = 40;
		const UBOOL bLPressed = State->ltrig >= TriggerThresh;
		const UBOOL bRPressed = State->rtrig >= TriggerThresh;
		const UBOOL bLDown = Input && Input->KeyDown(IK_JoyZ);
		const UBOOL bRDown = Input && Input->KeyDown(IK_JoyR);

		if( bLPressed && !bLDown )
			CauseInputEvent( IK_JoyZ, IST_Press );
		else if( !bLPressed && bLDown )
			CauseInputEvent( IK_JoyZ, IST_Release );

		if( bRPressed && !bRDown )
			CauseInputEvent( IK_JoyR, IST_Press );
		else if( !bRPressed && bRDown )
			CauseInputEvent( IK_JoyR, IST_Release );
	}

	// Menu cursor + analog navigation.
	if( bInMenu )
	{
		bShowWindowsMouse = 1;
		bWindowsMouseAvailable = 1;
		SelectedCursor = 0;

		// Cursor (absolute) - drive with stick or DPad
		const FLOAT Dead = 8.0f;
		const FLOAT SpeedPxPerSec = 380.0f; // tuned for 640x480
		FLOAT NX = 0.0f, NY = 0.0f;

		// Stick
		if( Abs(State->joyx) > Dead ) NX = (FLOAT)State->joyx / 128.0f;
		if( Abs(State->joyy) > Dead ) NY = (FLOAT)State->joyy / 128.0f;

		// DPad fallback (useful if stick is centered)
		if( NX == 0.0f )
		{
			if( Buttons & CONT_DPAD_LEFT ) NX = -1.0f;
			else if( Buttons & CONT_DPAD_RIGHT ) NX = 1.0f;
		}
		if( NY == 0.0f )
		{
			if( Buttons & CONT_DPAD_UP ) NY = -1.0f;
			else if( Buttons & CONT_DPAD_DOWN ) NY = 1.0f;
		}

		if( NX != 0.0f || NY != 0.0f )
		{
			const FLOAT DX = NX * SpeedPxPerSec * DeltaTime;
			const FLOAT DY = NY * SpeedPxPerSec * DeltaTime;

			WindowsMouseX = Clamp( WindowsMouseX + DX, 0.0f, (FLOAT)(SizeX-1) );
			WindowsMouseY = Clamp( WindowsMouseY + DY, 0.0f, (FLOAT)(SizeY-1) );
			Client->Engine->MousePosition( this, 0, WindowsMouseX, WindowsMouseY );

			// Also emit mouse axis events for UWindow paths that listen to IK_MouseX/Y deltas.
			CauseInputEvent( IK_MouseX, IST_Axis, DX );
			// Match WinDrv sign convention: positive screen DY is emitted as negative IK_MouseY delta.
			CauseInputEvent( IK_MouseY, IST_Axis, -DY );
		}

		const INT Th = 32;
		const FLOAT FirstDelay = 0.30f;
		const FLOAT RepeatDelay = 0.20f;

		MenuNavTimerX += DeltaTime;
		MenuNavTimerY += DeltaTime;

		// Direction from stick only (DPad tap/edges are handled above).
		INT DirX = 0;
		INT DirY = 0;

		if( State->joyx < -Th ) DirX = -1;
		else if( State->joyx > Th ) DirX = 1;

		if( State->joyy < -Th ) DirY = -1;
		else if( State->joyy > Th ) DirY = 1;

		// X-axis repeat
		if( DirX != 0 )
		{
			const UBOOL bChanged = (DirX != MenuNavDirX);
			const FLOAT Delay = (MenuNavDirX == 0 || bChanged) ? 0.0f : (MenuNavTimerX < FirstDelay ? FirstDelay : RepeatDelay);
			if( bChanged || MenuNavTimerX >= Delay )
			{
				PulseKey( DirX < 0 ? IK_Left : IK_Right );
				MenuNavTimerX = 0.0f;
			}
		}
		else
		{
			MenuNavTimerX = 0.0f;
		}
		MenuNavDirX = DirX;

		// Y-axis repeat
		if( DirY != 0 )
		{
			const UBOOL bChanged = (DirY != MenuNavDirY);
			const FLOAT Delay = (MenuNavDirY == 0 || bChanged) ? 0.0f : (MenuNavTimerY < FirstDelay ? FirstDelay : RepeatDelay);
			if( bChanged || MenuNavTimerY >= Delay )
			{
				PulseKey( DirY < 0 ? IK_Up : IK_Down );
				MenuNavTimerY = 0.0f;
			}
		}
		else
		{
			MenuNavTimerY = 0.0f;
		}
		MenuNavDirY = DirY;
	}
	else
	{
		bShowWindowsMouse = 0;
		MenuNavTimerX = 0.0f;
		MenuNavTimerY = 0.0f;
		MenuNavDirX = 0;
		MenuNavDirY = 0;
	}
}

void UKOSViewport::TickKeyboard( maple_device_t* Dev, const FLOAT DeltaTime )
{
	kbd_state_t* State = (kbd_state_t*)maple_dev_status( Dev );
	if( !State )
		return;

	// Emit key events for the regular input system
	appMemcpy( KeyStatePrev, KeyState, sizeof( KeyState ) );
	appMemcpy( KeyState, State->matrix, sizeof( KeyState ) );
	for( INT i = 0; i < MAX_KBD_KEYS; ++i )
	{
		if( KeyMap[i] )
		{
			if( KeyState[i] && !KeyStatePrev[i] )
				CauseInputEvent( KeyMap[i], IST_Press );
			else if( !KeyState[i] && KeyStatePrev[i ])
				CauseInputEvent( KeyMap[i], IST_Release );
		}
	}

	// Emit text input
	const INT Chr = kbd_queue_pop( Dev, true );
	if( Chr > 0 ) {
		if( isprint( Chr ) || Chr == '\r' )
			Client->Engine->Key( this, (EInputKey)Chr );
	}
}

UBOOL UKOSViewport::TickInput()
{
	guard(UKOSViewport::TickInput);

	maple_device_t* Dev;
	const FLOAT CurTime = appSeconds();
	const FLOAT DeltaTime = CurTime - InputUpdateTime;

	// Check keyboard
	Dev = maple_enum_type( 0, MAPLE_FUNC_KEYBOARD );
	if ( Dev )
		TickKeyboard( Dev, DeltaTime );

	// Check joystick
	Dev = maple_enum_type( 0, MAPLE_FUNC_CONTROLLER );
	if( Dev )
		TickJoystick( Dev, DeltaTime );

	InputUpdateTime = CurTime;

	return QuitRequested;

	unguard;
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

UBOOL UKOSViewport::Exec( const TCHAR* Cmd, FOutputDevice& Out )
{
	guard(UKOSViewport::Exec);
	if( UViewport::Exec( Cmd, Out ) )
	{
		return 1;
	}
	else if( ParseCommand(&Cmd, "ToggleFullscreen") )
	{
		// Toggle fullscreen.
		if( Client->FullscreenViewport )
			Client->EndFullscreen();
		else if( !(Actor->ShowFlags & SHOW_ChildWindow) )
			Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", 1 );
		return 1;
	}
	else if( ParseCommand(&Cmd, "GetCurrentRes") )
	{
		Out.Logf( TEXT("%ix%i"), SizeX, SizeY );
		return 1;
	}
	else if( ParseCommand(&Cmd, "SetRes") )
	{
		INT X=appAtoi(Cmd), Y=appAtoi(appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : "");
		if( X && Y )
		{
			if( Client->FullscreenViewport )
				MakeFullscreen( X, Y, 1 );
			else
				SetClientSize( X, Y, 1 );
		}
		return 1;
	}
	else if( ParseCommand(&Cmd, "Preferences") )
	{
		if( Client->FullscreenViewport )
			Client->EndFullscreen();
		return 1;
	}
	else return 0;
	unguard;
}
