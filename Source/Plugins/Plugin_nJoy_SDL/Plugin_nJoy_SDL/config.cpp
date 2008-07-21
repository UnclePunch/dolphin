//////////////////////////////////////////////////////////////////////////////////////////
// Project description
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
// Name: nJoy 
// Description: A Dolphin Compatible Input Plugin
//
// Author: Falcon4ever (nJoy@falcon4ever.com)
// Site: www.multigesture.net
// Copyright (C) 2003-2008 Dolphin Project.
//
//////////////////////////////////////////////////////////////////////////////////////////
//
// Licensetype: GNU General Public License (GPL)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.
//
// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/
//
// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/
//
//////////////////////////////////////////////////////////////////////////////////////////

#include "nJoy.h"

extern CONTROLLER_INFO	*joyinfo;
extern CONTROLLER_MAPPING joysticks[4];
extern bool emulator_running;

// Create dialog and pages
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
int OpenConfig(HINSTANCE hInst, HWND _hParent)
{
	PROPSHEETPAGE psp[4];
    PROPSHEETHEADER psh;

    psp[0].dwSize = sizeof(PROPSHEETPAGE);
    psp[0].dwFlags = PSP_USETITLE;
    psp[0].hInstance = hInst;
    psp[0].pszTemplate = MAKEINTRESOURCE(IDD_CONFIG);
    psp[0].pszIcon = NULL;
    psp[0].pfnDlgProc = (DLGPROC)ControllerTab1;
    psp[0].pszTitle = "Controller 1";
    psp[0].lParam = 0;

    psp[1].dwSize = sizeof(PROPSHEETPAGE);
    psp[1].dwFlags = PSP_USETITLE;
    psp[1].hInstance = hInst;
    psp[1].pszTemplate = MAKEINTRESOURCE(IDD_CONFIG);
    psp[1].pszIcon = NULL;
    psp[1].pfnDlgProc = (DLGPROC)ControllerTab2;
    psp[1].pszTitle = "Controller 2";
    psp[1].lParam = 0;
    
	psp[2].dwSize = sizeof(PROPSHEETPAGE);
    psp[2].dwFlags = PSP_USETITLE;
    psp[2].hInstance = hInst;
    psp[2].pszTemplate = MAKEINTRESOURCE(IDD_CONFIG);
    psp[2].pszIcon = NULL;
    psp[2].pfnDlgProc = (DLGPROC)ControllerTab3;
    psp[2].pszTitle = "Controller 3";
    psp[2].lParam = 0;

	psp[3].dwSize = sizeof(PROPSHEETPAGE);
    psp[3].dwFlags = PSP_USETITLE;
    psp[3].hInstance = hInst;
    psp[3].pszTemplate = MAKEINTRESOURCE(IDD_CONFIG);
    psp[3].pszIcon = NULL;
    psp[3].pfnDlgProc = (DLGPROC)ControllerTab4;
    psp[3].pszTitle = "Controller 4";
    psp[3].lParam = 0;

    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP;	
    psh.hwndParent = _hParent;
    psh.hInstance = hInst;
    psh.pszIcon = NULL;
    #ifndef _DEBUG
		psh.pszCaption = (LPSTR) "Configure: nJoy v"INPUT_VERSION " Input Plugin";
	#else
		psh.pszCaption = (LPSTR) "Configure: nJoy v"INPUT_VERSION " (Debug) Input Plugin";		
	#endif
    psh.nPages = sizeof(psp) / sizeof(PROPSHEETPAGE);
    psh.ppsp = (LPCPROPSHEETPAGE) &psp;
	
	return (int)(PropertySheet(&psh));
}

// Create Tab
// ŻŻŻŻŻŻŻŻŻŻ
BOOL APIENTRY ControllerTab1(HWND hDlg, UINT message, UINT wParam, LONG lParam)
{
	return ControllerTab(hDlg, message, wParam, lParam, 0);	
}

BOOL APIENTRY ControllerTab2(HWND hDlg, UINT message, UINT wParam, LONG lParam)
{
	return ControllerTab(hDlg, message, wParam, lParam, 1);	
}

BOOL APIENTRY ControllerTab3(HWND hDlg, UINT message, UINT wParam, LONG lParam)
{
	return ControllerTab(hDlg, message, wParam, lParam, 2);	
}

BOOL APIENTRY ControllerTab4(HWND hDlg, UINT message, UINT wParam, LONG lParam)
{
	return ControllerTab(hDlg, message, wParam, lParam, 3);	
}

// Create Controller Tab
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
BOOL ControllerTab(HWND hDlg, UINT message, UINT wParam, LONG lParam, int controller)
{
	switch (message)
	{
		case WM_INITDIALOG:		
			// Prevent user from changing the joystick while emulation is running
			if(emulator_running)
			{
				ComboBox_Enable(GetDlgItem(hDlg, IDC_JOYNAME), FALSE);
				Button_Enable(GetDlgItem(hDlg, IDC_JOYATTACH), FALSE);
			}
			else
			{
				ComboBox_Enable(GetDlgItem(hDlg, IDC_JOYNAME), TRUE);
				Button_Enable(GetDlgItem(hDlg, IDC_JOYATTACH), TRUE);
			}
			
			// Search for devices and add the to the device list
			if(Search_Devices())
			{
				HWND CB = GetDlgItem(hDlg, IDC_JOYNAME);		
				for(int x = 0; x < SDL_NumJoysticks(); x++)
				{
					SendMessage(CB, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((LPCTSTR)joyinfo[x].Name));
				}
				
				char buffer [8];				
				CB = GetDlgItem(hDlg, IDC_DEADZONE);
				SendMessage(CB, CB_RESETCONTENT, 0, 0);
				for(int x = 1; x <= 100; x++)
				{		
					sprintf (buffer, "%d %%", x);
					SendMessage(CB, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>((LPCTSTR)buffer));
				}

				SetControllerAll(hDlg, controller);
				return TRUE;
			}
			else
			{
				HWND CB = GetDlgItem(hDlg, IDC_JOYNAME);				
				SendMessage(CB, CB_ADDSTRING, 0, (LPARAM)"No Joystick detected!");
				SendMessage(CB, CB_SETCURSEL, 0, 0);
				return FALSE;
			}			
		break;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDC_JOYNAME:
				{
					// Selected a different joystick
					if(HIWORD(wParam) == CBN_SELCHANGE)
					{
						joysticks[controller].ID = (int)SendMessage(GetDlgItem(hDlg, IDC_JOYNAME), CB_GETCURSEL, 0, 0);
					}
					return TRUE;
				}
				break;

				case IDC_SHOULDERL:
				case IDC_SHOULDERR:
				case IDC_A:
				case IDC_B:
				case IDC_X:
				case IDC_Y:
				case IDC_Z:
				case IDC_START:
				{
					GetButtons(hDlg, LOWORD(wParam), controller);
					return TRUE;
				}
				break;
				
				case IDC_DPAD:
				{
					GetHats(hDlg, LOWORD(wParam), controller);
					return TRUE;
				}
				break;

				case IDC_SX:
				case IDC_SY:
				case IDC_MX:
				case IDC_MY:
				{
					GetAxis(hDlg, LOWORD(wParam), controller);
					return TRUE;
				}
				break;
			}			
		break;
				
		case WM_DESTROY:			
			GetControllerAll(hDlg, controller);
		break;
	}	
	return FALSE;	
}

// Wait for button press
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
bool GetButtons(HWND hDlg, int buttonid, int controller)
{
	buttonid += 1000;
		
	SDL_Joystick *joy;
	joy=SDL_JoystickOpen(joysticks[controller].ID);

	char format[128];
	int buttons = SDL_JoystickNumButtons(joy);
	bool waiting = true;
	bool succeed = false;
	int pressed = 0;

	int counter1 = 0;
	int counter2 = 10;
	
	sprintf(format, "[%d]", counter2);
	SetDlgItemText(hDlg, buttonid, format);
	
	while(waiting)
	{			
		SDL_JoystickUpdate();
		for(int b = 0; b < buttons; b++)
		{			
			if(SDL_JoystickGetButton(joy, b))
			{
				pressed = b;	
				waiting = false;
				succeed = true;
				break;
			}			
		}

		counter1++;
		if(counter1==100)
		{
			counter1=0;
			counter2--;
			
			sprintf(format, "[%d]", counter2);
			SetDlgItemText(hDlg, buttonid, format);

			if(counter2<0)
				waiting = false;
		}	
		Sleep(10);
	}

	if(succeed)
		sprintf(format, "%d", pressed);
	else
		sprintf(format, "-1", pressed);
	SetDlgItemText(hDlg, buttonid, format);

	if(SDL_JoystickOpened(joysticks[controller].ID))
		SDL_JoystickClose(joy);

	return true;
}

// Wait for D-Pad
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
bool GetHats(HWND hDlg, int buttonid, int controller)
{
	buttonid += 1000;

	SDL_Joystick *joy;
	joy=SDL_JoystickOpen(joysticks[controller].ID);

	char format[128];
	int hats = SDL_JoystickNumHats(joy);
	bool waiting = true;
	bool succeed = false;
	int pressed = 0;

	int counter1 = 0;
	int counter2 = 10;
	
	sprintf(format, "[%d]", counter2);
	SetDlgItemText(hDlg, buttonid, format);

	while(waiting)
	{			
		SDL_JoystickUpdate();
		for(int b = 0; b < hats; b++)
		{			
			if(SDL_JoystickGetHat(joy, b))
			{
				pressed = b;	
				waiting = false;
				succeed = true;
				break;
			}			
		}

		counter1++;
		if(counter1==100)
		{
			counter1=0;
			counter2--;
			
			sprintf(format, "[%d]", counter2);
			SetDlgItemText(hDlg, buttonid, format);

			if(counter2<0)
				waiting = false;
		}	
		Sleep(10);
	}

	if(succeed)
		sprintf(format, "%d", pressed);
	else
		sprintf(format, "-1", pressed);
	SetDlgItemText(hDlg, buttonid, format);

	if(SDL_JoystickOpened(joysticks[controller].ID))
		SDL_JoystickClose(joy);

	return true;
}

// Wait for Analog
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
bool GetAxis(HWND hDlg, int buttonid, int controller)
{
	buttonid += 1000;

	SDL_Joystick *joy;
	joy=SDL_JoystickOpen(joysticks[controller].ID);

	char format[128];
	int axes = SDL_JoystickNumAxes(joy);
	bool waiting = true;
	bool succeed = false;
	int pressed = 0;
	Sint16 value;
	
	int counter1 = 0;
	int counter2 = 10;
	
	sprintf(format, "[%d]", counter2);
	SetDlgItemText(hDlg, buttonid, format);

	while(waiting)
	{		
		SDL_JoystickUpdate();
		for(int b = 0; b < axes; b++)
		{		
			value = SDL_JoystickGetAxis(joy, b);
			if(value < -10000 || value > 10000)
			{
				pressed = b;	
				waiting = false;
				succeed = true;
				break;
			}			
		}	

		counter1++;
		if(counter1==100)
		{
			counter1=0;
			counter2--;
			
			sprintf(format, "[%d]", counter2);
			SetDlgItemText(hDlg, buttonid, format);

			if(counter2<0)
				waiting = false;
		}	
		Sleep(10);
	}

	if(succeed)
		sprintf(format, "%d", pressed);
	else
		sprintf(format, "-1", pressed);
	SetDlgItemText(hDlg, buttonid, format);

	if(SDL_JoystickOpened(joysticks[controller].ID))
		SDL_JoystickClose(joy);

	return true;
}

// Set dialog items
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
void SetControllerAll(HWND hDlg, int controller)
{	
	SendMessage(GetDlgItem(hDlg, IDC_JOYNAME), CB_SETCURSEL, joysticks[controller].ID, 0);

	SetButton(hDlg, IDTEXT_SHOULDERL, joysticks[controller].buttons[CTL_L_SHOULDER]);
	SetButton(hDlg, IDTEXT_SHOULDERR, joysticks[controller].buttons[CTL_R_SHOULDER]);
	SetButton(hDlg, IDTEXT_A, joysticks[controller].buttons[CTL_A_BUTTON]);
	SetButton(hDlg, IDTEXT_B, joysticks[controller].buttons[CTL_B_BUTTON]);
	SetButton(hDlg, IDTEXT_X, joysticks[controller].buttons[CTL_X_BUTTON]);
	SetButton(hDlg, IDTEXT_Y, joysticks[controller].buttons[CTL_Y_BUTTON]);
	SetButton(hDlg, IDTEXT_Z, joysticks[controller].buttons[CTL_Z_TRIGGER]);
	SetButton(hDlg, IDTEXT_START, joysticks[controller].buttons[CTL_START]);
	
	SetButton(hDlg, IDTEXT_DPAD, joysticks[controller].dpad);
	
	SetButton(hDlg, IDTEXT_MX, joysticks[controller].axis[CTL_MAIN_X]);
	SetButton(hDlg, IDTEXT_MY, joysticks[controller].axis[CTL_MAIN_Y]);
	SetButton(hDlg, IDTEXT_SX, joysticks[controller].axis[CTL_SUB_X]);
	SetButton(hDlg, IDTEXT_SY, joysticks[controller].axis[CTL_SUB_Y]);

	SendDlgItemMessage(hDlg, IDC_JOYATTACH, BM_SETCHECK, joysticks[controller].enabled, 0);

	SendMessage(GetDlgItem(hDlg, IDC_DEADZONE), CB_SETCURSEL, joysticks[controller].deadzone, 0);	
}

// Get dialog items
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
void GetControllerAll(HWND hDlg, int controller)
{
	joysticks[controller].ID = (int)SendMessage(GetDlgItem(hDlg, IDC_JOYNAME), CB_GETCURSEL, 0, 0); 

	joysticks[controller].buttons[CTL_L_SHOULDER] = GetButton(hDlg, IDTEXT_SHOULDERL);
	joysticks[controller].buttons[CTL_R_SHOULDER] = GetButton(hDlg, IDTEXT_SHOULDERR);
	joysticks[controller].buttons[CTL_A_BUTTON] = GetButton(hDlg, IDTEXT_A);
	joysticks[controller].buttons[CTL_B_BUTTON] = GetButton(hDlg, IDTEXT_B);
	joysticks[controller].buttons[CTL_X_BUTTON] = GetButton(hDlg, IDTEXT_X);
	joysticks[controller].buttons[CTL_Y_BUTTON] = GetButton(hDlg, IDTEXT_Y);
	joysticks[controller].buttons[CTL_Z_TRIGGER] = GetButton(hDlg, IDTEXT_Z);
	joysticks[controller].buttons[CTL_START] = GetButton(hDlg, IDTEXT_START);

	joysticks[controller].dpad = GetButton(hDlg, IDTEXT_DPAD);

	joysticks[controller].axis[CTL_MAIN_X] = GetButton(hDlg, IDTEXT_MX);
	joysticks[controller].axis[CTL_MAIN_Y] = GetButton(hDlg, IDTEXT_MY);
	joysticks[controller].axis[CTL_SUB_X] = GetButton(hDlg, IDTEXT_SX);
	joysticks[controller].axis[CTL_SUB_Y] = GetButton(hDlg, IDTEXT_SY);

	joysticks[controller].enabled = (int)SendMessage(GetDlgItem(hDlg, IDC_JOYATTACH), BM_GETCHECK, 0, 0);
	
	joysticks[controller].deadzone = (int)SendMessage(GetDlgItem(hDlg, IDC_DEADZONE), CB_GETCURSEL, 0, 0);
}

// Set text in static text item
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
void SetButton(HWND hDlg, int item, int value)
{
	char format[8];
	sprintf(format, "%d", value);
	SetDlgItemText(hDlg, item, format);
}

// Get text from static text item
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ
int GetButton(HWND hDlg, int item)
{
	char format[8];
	GetDlgItemText(hDlg, item, format, sizeof(format));
	return atoi(format);
}

//////////////////////////////////////////////////////////////////////////////////////////
// About dialog functions
// ŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻŻ

void OpenAbout(HINSTANCE hInst, HWND _hParent)
{
	DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUT), _hParent, (DLGPROC)AboutDlg);
}

BOOL CALLBACK AboutDlg(HWND abouthWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	char format[0xFFF];	
	
	switch (message)
	{
		case WM_INITDIALOG:
			sprintf(format,"nJoy v"INPUT_VERSION" by Falcon4ever\n"
			"Release: "RELDAY"/"RELMONTH"/"RELYEAR"\n"
			"www.multigesture.net");
			SetDlgItemText(abouthWnd,IDC_ABOUT_TEXT,format);

			sprintf(format, THANKYOU);
			SetDlgItemText(abouthWnd,IDC_ABOUT_TEXT2,format);
			
			sprintf(format, INPUT_STATE);
			SetDlgItemText(abouthWnd,IDC_ABOUT_TEXT3,format);
		break;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK) 
			{
				EndDialog(abouthWnd, LOWORD(wParam));
				return TRUE;
			}
		break;
	}

    return FALSE;
}
