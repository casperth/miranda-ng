/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (�) 2012-17 Miranda NG project (http://miranda-ng.org)
Copyright (c) 2000-04 Miranda ICQ/IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR GetAvatarBitmap(WPARAM hContact, LPARAM)
{
	if (hContact == 0 || g_shutDown || fei == NULL)
		return 0;

	hContact = GetContactThatHaveTheAvatar(hContact);

	// Get the node
	CacheNode *node = FindAvatarInCache(hContact, true);
	if (node == NULL || !node->loaded)
		return (INT_PTR)GetProtoDefaultAvatar(hContact);
	return (INT_PTR)node;
}

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR ProtectAvatar(WPARAM hContact, LPARAM lParam)
{
	BYTE was_locked = db_get_b(hContact, "ContactPhoto", "Locked", 0);

	if (fei == NULL || was_locked == (BYTE)lParam)      // no need for redundant lockings...
		return 0;

	if (hContact) {
		if (!was_locked)
			MakePathRelative(hContact);
		db_set_b(hContact, "ContactPhoto", "Locked", lParam ? 1 : 0);
		if (lParam == 0)
			MakePathRelative(hContact);
		ChangeAvatar(hContact, true);
	}
	return 0;
}

/*
 * set an avatar (service function)
 * if lParam == NULL, a open file dialog will be opened, otherwise, lParam is taken as a FULL
 * image filename (will be checked for existance, though)
 */

struct OpenFileSubclassData
{
	BYTE *locking_request;
	BYTE setView;
};

UINT_PTR CALLBACK OpenFileSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	OpenFileSubclassData *data = (OpenFileSubclassData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	switch (msg) {
	case WM_INITDIALOG:
		TranslateDialogDefault(hwnd);
		{
			OPENFILENAME *ofn = (OPENFILENAME *)lParam;

			data = (OpenFileSubclassData *)malloc(sizeof(OpenFileSubclassData));
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
			data->locking_request = (BYTE *)ofn->lCustData;
			data->setView = TRUE;
			CheckDlgButton(hwnd, IDC_PROTECTAVATAR, *(data->locking_request) ? BST_CHECKED : BST_UNCHECKED);
		}
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_PROTECTAVATAR)
			*(data->locking_request) = IsDlgButtonChecked(hwnd, IDC_PROTECTAVATAR) ? TRUE : FALSE;
		break;

	case WM_NOTIFY:
		if (data->setView) {
			HWND hwndParent = GetParent(hwnd);
			HWND hwndLv = FindWindowEx(hwndParent, NULL, L"SHELLDLL_DefView", NULL);
			if (hwndLv != NULL) {
				SendMessage(hwndLv, WM_COMMAND, SHVIEW_THUMBNAIL, 0);
				data->setView = FALSE;
			}
		}
		break;

	case WM_NCDESTROY:
		free((OpenFileSubclassData *)data);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)0);
		break;
	}

	return FALSE;
}

INT_PTR SetAvatar(WPARAM hContact, LPARAM lParam)
{
	wchar_t FileName[MAX_PATH];
	wchar_t *szFinalName;
	BYTE locking_request;

	if (hContact == NULL || fei == NULL)
		return 0;

	int is_locked = db_get_b(hContact, "ContactPhoto", "Locked", 0);

	wchar_t *tszPath = (wchar_t*)lParam;
	if (tszPath == NULL) {
		wchar_t filter[256];
		Bitmap_GetFilter(filter, _countof(filter));

		OPENFILENAME ofn = { 0 };
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = 0;
		ofn.lpstrFile = FileName;
		ofn.lpstrFilter = filter;
		ofn.nMaxFile = MAX_PATH;
		ofn.nMaxFileTitle = MAX_PATH;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_ENABLETEMPLATE | OFN_EXPLORER | OFN_ENABLESIZING | OFN_ENABLEHOOK;
		ofn.lpstrInitialDir = L".";
		*FileName = '\0';
		ofn.lpstrDefExt = L"";
		ofn.hInstance = g_hInst;
		ofn.lpTemplateName = MAKEINTRESOURCE(IDD_OPENSUBCLASS);
		ofn.lpfnHook = OpenFileSubclass;
		locking_request = is_locked;
		ofn.lCustData = (LPARAM)&locking_request;
		if (!GetOpenFileName(&ofn))
			return 0;

		szFinalName = FileName;
		is_locked = locking_request ? 1 : is_locked;
	}
	else szFinalName = tszPath;

	// filename is now set, check it and perform all needed action
	if (_waccess(szFinalName, 4) == -1)
		return 0;

	// file exists...
	wchar_t szBackupName[MAX_PATH];
	PathToRelativeW(szFinalName, szBackupName, g_szDataPath);
	db_set_ws(hContact, "ContactPhoto", "Backup", szBackupName);

	db_set_b(hContact, "ContactPhoto", "Locked", is_locked);
	db_set_ws(hContact, "ContactPhoto", "File", szFinalName);
	MakePathRelative(hContact, szFinalName);

	// Fix cache
	ChangeAvatar(hContact, true);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
// see if is possible to set the avatar for the expecified protocol

static INT_PTR CanSetMyAvatar(WPARAM wParam, LPARAM)
{
	char *protocol = (char *)wParam;
	if (protocol == NULL || fei == NULL)
		return 0;

	return ProtoServiceExists(protocol, PS_SETMYAVATAR);
}

/////////////////////////////////////////////////////////////////////////////////////////
// set an avatar for a protocol(service function)
// if lParam == NULL, a open file dialog will be opened, otherwise, lParam is taken as a FULL
// image filename (will be checked for existance, though)

static int InternalRemoveMyAvatar(char *protocol)
{
	SetIgnoreNotify(protocol, TRUE);

	// Remove avatar
	int ret = 0;
	if (protocol != NULL) {
		if (ProtoServiceExists(protocol, PS_SETMYAVATAR))
			ret = SaveAvatar(protocol, NULL);
		else
			ret = -3;

		if (ret == 0) {
			// Has global avatar?
			DBVARIANT dbv = { 0 };
			if (!db_get_ws(NULL, AVS_MODULE, "GlobalUserAvatarFile", &dbv)) {
				db_free(&dbv);
				db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 1);
				DeleteGlobalUserAvatar();
			}
		}
	}
	else {
		PROTOACCOUNT **accs;
		int i, count;

		Proto_EnumAccounts(&count, &accs);
		for (i = 0; i < count; i++) {
			if (!ProtoServiceExists(accs[i]->szModuleName, PS_SETMYAVATAR))
				continue;

			if (!Proto_IsAvatarsEnabled(accs[i]->szModuleName))
				continue;

			// Found a protocol
			int retTmp = SaveAvatar(accs[i]->szModuleName, NULL);
			if (retTmp != 0)
				ret = retTmp;
		}

		DeleteGlobalUserAvatar();

		if (ret)
			db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 1);
		else
			db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 0);
	}

	SetIgnoreNotify(protocol, FALSE);

	ReportMyAvatarChanged(WPARAM((protocol == NULL) ? "" : protocol), 0);
	return ret;
}

static void FilterGetStrings(CMStringW &filter, BOOL xml, BOOL swf)
{
	filter.AppendFormat(L"%s (*.bmp;*.jpg;*.gif;*.png", TranslateT("All files"));
	if (swf) filter.Append(L";*.swf");
	if (xml) filter.Append(L";*.xml");

	filter.AppendFormat(L")%c*.BMP;*.RLE;*.JPG;*.JPEG;*.GIF;*.PNG", 0);
	if (swf) filter.Append(L";*.SWF");
	if (xml) filter.Append(L";*.XML");
	filter.AppendChar(0);

	filter.AppendFormat(L"%s (*.bmp;*.rle)%c*.BMP;*.RLE%c", TranslateT("Windows bitmaps"), 0, 0);
	filter.AppendFormat(L"%s (*.jpg;*.jpeg)%c*.JPG;*.JPEG%c", TranslateT("JPEG bitmaps"), 0, 0);
	filter.AppendFormat(L"%s (*.gif)%c*.GIF%c", TranslateT("GIF bitmaps"), 0, 0);
	filter.AppendFormat(L"%s (*.png)%c*.PNG%c", TranslateT("PNG bitmaps"), 0, 0);

	if (swf)
		filter.AppendFormat(L"%s (*.swf)%c*.SWF%c", TranslateT("Flash animations"), 0, 0);

	if (xml)
		filter.AppendFormat(L"%s (*.xml)%c*.XML%c", TranslateT("XML files"), 0, 0);

	filter.AppendChar(0);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Callback to set thumbnaill view to open dialog

static UINT_PTR CALLBACK SetMyAvatarHookProc(HWND hwnd, UINT msg, WPARAM, LPARAM lParam)
{
	OPENFILENAME *ofn = (OPENFILENAME *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	SetMyAvatarHookData *data;

	switch (msg) {
	case WM_INITDIALOG:
		hwndSetMyAvatar = hwnd;

		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lParam);
		ofn = (OPENFILENAME *)lParam;

		data = (SetMyAvatarHookData *)ofn->lCustData;
		data->thumbnail = TRUE;

		SetDlgItemText(hwnd, IDC_MAKE_SQUARE, TranslateT("Make the avatar square"));
		SetDlgItemText(hwnd, IDC_GROW, TranslateT("Grow avatar to fit max allowed protocol size"));

		CheckDlgButton(hwnd, IDC_MAKE_SQUARE, data->square ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(hwnd, IDC_GROW, data->grow ? BST_CHECKED : BST_UNCHECKED);

		if (data->protocol != NULL && (Proto_AvatarImageProportion(data->protocol) & PIP_SQUARE))
			EnableWindow(GetDlgItem(hwnd, IDC_MAKE_SQUARE), FALSE);
		break;

	case WM_NOTIFY:
		data = (SetMyAvatarHookData *)ofn->lCustData;
		if (data->thumbnail) {
			HWND hwndParent = GetParent(hwnd);
			HWND hwndLv = FindWindowEx(hwndParent, NULL, L"SHELLDLL_DefView", NULL);
			if (hwndLv != NULL) {
				SendMessage(hwndLv, WM_COMMAND, SHVIEW_THUMBNAIL, 0);
				data->thumbnail = FALSE;
			}
		}
		break;

	case WM_DESTROY:
		data = (SetMyAvatarHookData *)ofn->lCustData;
		data->square = IsDlgButtonChecked(hwnd, IDC_MAKE_SQUARE);
		data->grow = IsDlgButtonChecked(hwnd, IDC_GROW);

		hwndSetMyAvatar = NULL;
		break;
	}

	return 0;
}

struct SaveProtocolData
{
	DWORD max_size;
	wchar_t image_file_name[MAX_PATH];
	BOOL saved;
	BOOL need_smaller_size;
	int width;
	int height;
	wchar_t temp_file[MAX_PATH];
	HBITMAP hBmpProto;
};

void SaveImage(SaveProtocolData &d, char *protocol, int format)
{
	if (!Proto_IsAvatarFormatSupported(protocol, format))
		return;

	mir_snwprintf(d.image_file_name, L"%s%s", d.temp_file, ProtoGetAvatarExtension(format));
	if (BmpFilterSaveBitmap(d.hBmpProto, d.image_file_name, format == PA_FORMAT_JPEG ? JPEG_QUALITYSUPERB : 0))
		return;

	if (d.max_size != 0 && GetFileSize(d.image_file_name) > d.max_size) {
		DeleteFile(d.image_file_name);

		if (format == PA_FORMAT_JPEG) {
			// Try with lower quality
			if (!BmpFilterSaveBitmap(d.hBmpProto, d.image_file_name, JPEG_QUALITYGOOD)) {
				if (GetFileSize(d.image_file_name) > d.max_size) {
					DeleteFile(d.image_file_name);
					d.need_smaller_size = TRUE;
				}
				else d.saved = TRUE;
			}
		}
		else d.need_smaller_size = TRUE;
	}
	else d.saved = TRUE;
}

static int SetProtoMyAvatar(char *protocol, HBITMAP hBmp, wchar_t *originalFilename, int originalFormat, BOOL square, BOOL grow)
{
	if (!ProtoServiceExists(protocol, PS_SETMYAVATAR))
		return -1;

	// If is swf or xml, just set it

	if (originalFormat == PA_FORMAT_SWF) {
		if (!Proto_IsAvatarFormatSupported(protocol, PA_FORMAT_SWF))
			return -1;

		return SaveAvatar(protocol, originalFilename);
	}

	if (originalFormat == PA_FORMAT_XML) {
		if (!Proto_IsAvatarFormatSupported(protocol, PA_FORMAT_XML))
			return -1;

		return SaveAvatar(protocol, originalFilename);
	}

	// Get protocol info
	SaveProtocolData d = { 0 };

	d.max_size = (DWORD)Proto_GetAvatarMaxFileSize(protocol);

	Proto_GetAvatarMaxSize(protocol, &d.width, &d.height);
	int orig_width = d.width;
	int orig_height = d.height;

	if (Proto_AvatarImageProportion(protocol) & PIP_SQUARE)
		square = TRUE;

	// Try to save until a valid image is found or we give up
	int num_tries = 0;
	do {
		// Lets do it
		ResizeBitmap rb;
		rb.size = sizeof(ResizeBitmap);
		rb.hBmp = hBmp;
		rb.max_height = d.height;
		rb.max_width = d.width;
		rb.fit = (grow ? 0 : RESIZEBITMAP_FLAG_DONT_GROW)
			| (square ? RESIZEBITMAP_MAKE_SQUARE : RESIZEBITMAP_KEEP_PROPORTIONS);

		d.hBmpProto = (HBITMAP)CallService(MS_IMG_RESIZE, WPARAM(&rb), 0);

		if (d.hBmpProto == NULL) {
			if (d.temp_file[0] != '\0')
				DeleteFile(d.temp_file);
			return -1;
		}

		// Check if can use original image
		if (d.hBmpProto == hBmp
			&& Proto_IsAvatarFormatSupported(protocol, originalFormat)
			&& (d.max_size == 0 || GetFileSize(originalFilename) < d.max_size)) {
			if (d.temp_file[0] != '\0')
				DeleteFile(d.temp_file);

			// Use original image
			return SaveAvatar(protocol, originalFilename);
		}

		// Create a temporary file (if was not created already)
		if (d.temp_file[0] == '\0') {
			d.temp_file[0] = '\0';
			if (GetTempPath(MAX_PATH, d.temp_file) == 0
				|| GetTempFileName(d.temp_file, L"mir_av_", 0, d.temp_file) == 0) {
				DeleteObject(d.hBmpProto);
				return -1;
			}
		}

		// Which format?

		// First try to use original format
		if (originalFormat != PA_FORMAT_BMP)
			SaveImage(d, protocol, originalFormat);

		if (!d.saved && originalFormat != PA_FORMAT_PNG)
			SaveImage(d, protocol, PA_FORMAT_PNG);

		if (!d.saved && originalFormat != PA_FORMAT_JPEG)
			SaveImage(d, protocol, PA_FORMAT_JPEG);

		if (!d.saved && originalFormat != PA_FORMAT_GIF)
			SaveImage(d, protocol, PA_FORMAT_GIF);

		if (!d.saved)
			SaveImage(d, protocol, PA_FORMAT_BMP);

		num_tries++;
		if (!d.saved && d.need_smaller_size && num_tries < 4) {
			// Cleanup
			if (d.hBmpProto != hBmp)
				DeleteObject(d.hBmpProto);

			// use a smaller size
			d.width = orig_width * (4 - num_tries) / 4;
			d.height = orig_height * (4 - num_tries) / 4;
		}
	} while (!d.saved && d.need_smaller_size && num_tries < 4);

	int ret;

	if (d.saved) {
		// Call proto service
		ret = SaveAvatar(protocol, d.image_file_name);
		DeleteFile(d.image_file_name);
	}
	else ret = -1;

	if (d.temp_file[0] != '\0')
		DeleteFile(d.temp_file);

	if (d.hBmpProto != hBmp)
		DeleteObject(d.hBmpProto);

	return ret;
}

static int InternalSetMyAvatar(char *protocol, wchar_t *szFinalName, SetMyAvatarHookData &data, BOOL allAcceptXML, BOOL allAcceptSWF)
{
	int format = ProtoGetAvatarFormat(szFinalName);
	if (format == PA_FORMAT_UNKNOWN || _waccess(szFinalName, 4) == -1)
		return -3;

	// file exists...
	HBITMAP hBmp = NULL;

	if (format == PA_FORMAT_SWF) {
		if (!allAcceptSWF)
			return -4;
	}
	else if (format == PA_FORMAT_XML) {
		if (!allAcceptXML)
			return -4;
	}
	else {
		// Try to open if is not a flash or XML
		hBmp = (HBITMAP)CallService(MS_IMG_LOAD, (WPARAM)szFinalName, IMGL_WCHAR);
		if (hBmp == NULL)
			return -4;
	}

	SetIgnoreNotify(protocol, TRUE);

	int ret = 0;
	if (protocol != NULL) {
		ret = SetProtoMyAvatar(protocol, hBmp, szFinalName, format, data.square, data.grow);
		if (ret == 0) {
			DeleteGlobalUserAvatar();
			db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 1);
		}
	}
	else {
		int count;
		PROTOACCOUNT **accs;
		Proto_EnumAccounts(&count, &accs);
		for (int i = 0; i < count; i++) {
			if (!ProtoServiceExists(accs[i]->szModuleName, PS_SETMYAVATAR))
				continue;

			if (!Proto_IsAvatarsEnabled(accs[i]->szModuleName))
				continue;

			int retTmp = SetProtoMyAvatar(accs[i]->szModuleName, hBmp, szFinalName, format, data.square, data.grow);
			if (retTmp != 0)
				ret = retTmp;
		}

		DeleteGlobalUserAvatar();

		if (ret)
			db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 1);
		else {
			// Copy avatar file to store as global one
			wchar_t globalFile[1024];
			BOOL saved = TRUE;
			if (FoldersGetCustomPathT(hGlobalAvatarFolder, globalFile, _countof(globalFile), L"")) {
				mir_snwprintf(globalFile, L"%s%s", g_szDataPath, L"GlobalAvatar");
				CreateDirectory(globalFile, NULL);
			}

			wchar_t *ext = wcsrchr(szFinalName, '.'); // Can't be NULL here
			if (format == PA_FORMAT_XML || format == PA_FORMAT_SWF) {
				mir_snwprintf(globalFile, L"%s\\my_global_avatar%s", globalFile, ext);
				CopyFile(szFinalName, globalFile, FALSE);
			}
			else {
				// Resize (to avoid too big avatars)
				ResizeBitmap rb = { 0 };
				rb.size = sizeof(ResizeBitmap);
				rb.hBmp = hBmp;
				rb.max_height = 300;
				rb.max_width = 300;
				rb.fit = (data.grow ? 0 : RESIZEBITMAP_FLAG_DONT_GROW)
					| (data.square ? RESIZEBITMAP_MAKE_SQUARE : RESIZEBITMAP_KEEP_PROPORTIONS);

				HBITMAP hBmpTmp = (HBITMAP)CallService(MS_IMG_RESIZE, WPARAM(&rb), 0);

				// Check if need to resize
				if (hBmpTmp == hBmp || hBmpTmp == NULL) {
					// Use original image
					mir_snwprintf(globalFile, L"%s\\my_global_avatar%s", globalFile, ext);
					CopyFile(szFinalName, globalFile, FALSE);
				}
				else {
					// Save as PNG
					mir_snwprintf(globalFile, L"%s\\my_global_avatar.png", globalFile);
					if (BmpFilterSaveBitmap(hBmpTmp, globalFile, 0))
						saved = FALSE;

					DeleteObject(hBmpTmp);
				}
			}

			if (saved) {
				wchar_t relFile[1024];
				if (PathToRelativeW(globalFile, relFile, g_szDataPath))
					db_set_ws(NULL, AVS_MODULE, "GlobalUserAvatarFile", relFile);
				else
					db_set_ws(NULL, AVS_MODULE, "GlobalUserAvatarFile", globalFile);

				db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 0);
			}
			else db_set_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 1);
		}
	}

	DeleteObject(hBmp);

	SetIgnoreNotify(protocol, FALSE);

	ReportMyAvatarChanged(WPARAM((protocol == NULL) ? "" : protocol), 0);
	return ret;
}

INT_PTR SetMyAvatar(WPARAM wParam, LPARAM lParam)
{
	wchar_t FileName[MAX_PATH];
	wchar_t *szFinalName = NULL;
	BOOL allAcceptXML;
	BOOL allAcceptSWF;

	// Protocol allow seting of avatar?
	char* protocol = (char*)wParam;
	if (protocol != NULL && !CanSetMyAvatar((WPARAM)protocol, 0))
		return -1;

	wchar_t* tszPath = (wchar_t*)lParam;
	if (tszPath == NULL && hwndSetMyAvatar != 0) {
		SetForegroundWindow(hwndSetMyAvatar);
		SetFocus(hwndSetMyAvatar);
		ShowWindow(hwndSetMyAvatar, SW_SHOW);
		return -2;
	}

	SetMyAvatarHookData data = { 0 };

	// Check for XML and SWF
	if (protocol == NULL) {
		allAcceptXML = TRUE;
		allAcceptSWF = TRUE;

		int count;
		PROTOACCOUNT **accs;
		Proto_EnumAccounts(&count, &accs);
		for (int i = 0; i < count; i++) {
			if (!ProtoServiceExists(accs[i]->szModuleName, PS_SETMYAVATAR))
				continue;

			if (!Proto_IsAvatarsEnabled(accs[i]->szModuleName))
				continue;

			allAcceptXML = allAcceptXML && Proto_IsAvatarFormatSupported(accs[i]->szModuleName, PA_FORMAT_XML);
			allAcceptSWF = allAcceptSWF && Proto_IsAvatarFormatSupported(accs[i]->szModuleName, PA_FORMAT_SWF);
		}

		data.square = db_get_b(0, AVS_MODULE, "SetAllwaysMakeSquare", 0);
	}
	else {
		allAcceptXML = Proto_IsAvatarFormatSupported(protocol, PA_FORMAT_XML);
		allAcceptSWF = Proto_IsAvatarFormatSupported(protocol, PA_FORMAT_SWF);

		data.protocol = protocol;
		data.square = (Proto_AvatarImageProportion(protocol) & PIP_SQUARE)
			|| db_get_b(0, AVS_MODULE, "SetAllwaysMakeSquare", 0);
	}

	if (tszPath == NULL) {
		data.protocol = protocol;

		CMStringW filter;
		FilterGetStrings(filter, allAcceptXML, allAcceptSWF);

		wchar_t inipath[1024];
		FoldersGetCustomPathT(hMyAvatarsFolder, inipath, _countof(inipath), L".");

		OPENFILENAME ofn = { 0 };
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = FileName;
		ofn.lpstrFilter = filter;
		ofn.nMaxFile = MAX_PATH;
		ofn.nMaxFileTitle = MAX_PATH;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_ENABLETEMPLATE | OFN_EXPLORER | OFN_ENABLESIZING | OFN_ENABLEHOOK;
		ofn.lpstrInitialDir = inipath;
		ofn.lpTemplateName = MAKEINTRESOURCE(IDD_SET_OWN_SUBCLASS);
		ofn.lpfnHook = SetMyAvatarHookProc;
		ofn.lCustData = (LPARAM)&data;

		*FileName = '\0';
		ofn.lpstrDefExt = L"";
		ofn.hInstance = g_hInst;

		wchar_t title[256];
		if (protocol == NULL)
			mir_snwprintf(title, TranslateT("Set my avatar"));
		else {
			wchar_t* prototmp = mir_a2u(protocol);
			mir_snwprintf(title, TranslateT("Set my avatar for %s"), prototmp);
			mir_free(prototmp);
		}
		ofn.lpstrTitle = title;

		if (!GetOpenFileName(&ofn))
			return 1;

		szFinalName = FileName;
	}
	else szFinalName = (wchar_t*)tszPath;

	// filename is now set, check it and perform all needed action
	if (szFinalName[0] == '\0')
		return InternalRemoveMyAvatar(protocol);

	return InternalSetMyAvatar(protocol, szFinalName, data, allAcceptXML, allAcceptSWF);
}

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR CALLBACK DlgProcAvatarOptions(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);

static INT_PTR ContactOptions(WPARAM wParam, LPARAM)
{
	if (wParam)
		CreateDialogParam(g_hInst, MAKEINTRESOURCE(IDD_AVATAROPTIONS), 0, DlgProcAvatarOptions, (LPARAM)wParam);
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR DrawAvatarPicture(WPARAM, LPARAM lParam)
{
	AVATARCACHEENTRY *ace = NULL;

	AVATARDRAWREQUEST *r = (AVATARDRAWREQUEST*)lParam;
	if (fei == NULL || r == NULL || IsBadReadPtr((void *)r, sizeof(AVATARDRAWREQUEST)))
		return 0;

	if (r->cbSize != sizeof(AVATARDRAWREQUEST))
		return 0;

	if (r->dwFlags & AVDRQ_PROTOPICT) {
		if (r->szProto == NULL)
			return 0;

		for (int i = 0; i < g_ProtoPictures.getCount(); i++) {
			protoPicCacheEntry& p = g_ProtoPictures[i];
			if (!mir_strcmp(p.szProtoname, r->szProto) && mir_strlen(r->szProto) == mir_strlen(p.szProtoname) && p.hbmPic != 0) {
				ace = (AVATARCACHEENTRY *)&g_ProtoPictures[i];
				break;
			}
		}
	}
	else if (r->dwFlags & AVDRQ_OWNPIC) {
		if (r->szProto == NULL)
			return 0;

		if (r->szProto[0] == '\0' && db_get_b(NULL, AVS_MODULE, "GlobalUserAvatarNotConsistent", 1))
			return -1;

		ace = (AVATARCACHEENTRY *)GetMyAvatar(0, (LPARAM)r->szProto);
	}
	else ace = (AVATARCACHEENTRY *)GetAvatarBitmap((WPARAM)r->hContact, 0);

	if (ace && (!(r->dwFlags & AVDRQ_RESPECTHIDDEN) || !(ace->dwFlags & AVS_HIDEONCLIST))) {
		ace->t_lastAccess = time(NULL);

		if (ace->bmHeight == 0 || ace->bmWidth == 0 || ace->hbmPic == 0)
			return 0;

		InternalDrawAvatar(r, ace->hbmPic, ace->bmWidth, ace->bmHeight, ace->dwFlags);
		return 1;
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR GetMyAvatar(WPARAM wParam, LPARAM lParam)
{
	if (wParam || g_shutDown || fei == NULL)
		return 0;

	char *szProto = (char *)lParam;
	if (lParam == 0 || IsBadReadPtr(szProto, 4))
		return 0;

	for (int i = 0; i < g_MyAvatars.getCount(); i++)
		if (!mir_strcmp(szProto, g_MyAvatars[i].szProtoname) && g_MyAvatars[i].hbmPic != 0)
			return (INT_PTR)&g_MyAvatars[i];

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static void ReloadMyAvatar(LPVOID lpParam)
{
	Thread_SetName("AVS: ReloadMyAvatar");

	char *szProto = (char *)lpParam;

	mir_sleep(500);
	for (int i = 0; !g_shutDown && i < g_MyAvatars.getCount(); i++) {
		char *myAvatarProto = g_MyAvatars[i].szProtoname;

		if (szProto[0] == 0) {
			// Notify to all possibles
			if (mir_strcmp(myAvatarProto, szProto)) {
				if (!ProtoServiceExists(myAvatarProto, PS_SETMYAVATAR))
					continue;
				if (!Proto_IsAvatarsEnabled(myAvatarProto))
					continue;
			}

		}
		else if (mir_strcmp(myAvatarProto, szProto))
			continue;

		if (g_MyAvatars[i].hbmPic)
			DeleteObject(g_MyAvatars[i].hbmPic);

		if (CreateAvatarInCache(INVALID_CONTACT_ID, &g_MyAvatars[i], myAvatarProto) != -1)
			NotifyEventHooks(hMyAvatarChanged, (WPARAM)myAvatarProto, (LPARAM)&g_MyAvatars[i]);
		else
			NotifyEventHooks(hMyAvatarChanged, (WPARAM)myAvatarProto, 0);
	}

	free(lpParam);
}

INT_PTR ReportMyAvatarChanged(WPARAM wParam, LPARAM)
{
	const char *proto = (const char*)wParam;
	if (proto == NULL)
		return -1;

	for (int i = 0; i < g_MyAvatars.getCount(); i++) {
		if (g_MyAvatars[i].dwFlags & AVS_IGNORENOTIFY)
			continue;

		if (!mir_strcmp(g_MyAvatars[i].szProtoname, proto)) {
			LPVOID lpParam = (void *)malloc(mir_strlen(g_MyAvatars[i].szProtoname) + 2);
			mir_strcpy((char *)lpParam, g_MyAvatars[i].szProtoname);
			mir_forkthread(ReloadMyAvatar, lpParam);
			return 0;
		}
	}

	return -2;
}

/////////////////////////////////////////////////////////////////////////////////////////

void InitServices()
{
	CreateServiceFunction(MS_AV_GETAVATARBITMAP, GetAvatarBitmap);
	CreateServiceFunction(MS_AV_PROTECTAVATAR, ProtectAvatar);
	CreateServiceFunction(MS_AV_SETAVATARW, SetAvatar);
	CreateServiceFunction(MS_AV_SETMYAVATARW, SetMyAvatar);
	CreateServiceFunction(MS_AV_CANSETMYAVATAR, CanSetMyAvatar);
	CreateServiceFunction(MS_AV_CONTACTOPTIONS, ContactOptions);
	CreateServiceFunction(MS_AV_DRAWAVATAR, DrawAvatarPicture);
	CreateServiceFunction(MS_AV_GETMYAVATAR, GetMyAvatar);
	CreateServiceFunction(MS_AV_REPORTMYAVATARCHANGED, ReportMyAvatarChanged);
}
