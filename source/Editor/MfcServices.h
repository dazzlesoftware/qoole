/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// MFC implementations of the Core services (Engine/Core/Public/Services),
// so the map core reaches the user through the MFC editor's UI:
//
//   IConfigStore    the CWinApp registry profile (existing settings load)
//   IMessageDialog  MessageBox
//   IProgress       CProgressWnd
//   IFileDialogs    CDirDialog
//   GLog            Display and more severe messages go to the output pane
//
// FApp's name comes from AfxGetAppName() and its exit request posts WM_QUIT.

#ifndef __MFCSERVICES_H
#define __MFCSERVICES_H

// Call at the start of InitInstance, after SetRegistryKey.
void RegisterMfcServices(void);

// Call at the end of ExitInstance.
void UnregisterMfcServices(void);

#endif
