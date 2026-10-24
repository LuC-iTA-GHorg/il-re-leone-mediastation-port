/* Il Re Leone - options window of the SDL2 port (launcher.exe). Plain Win32, no dependencies.
 *
 * Shows the display options of the port (see the comment at the top of engine/main.c), writes
 * them to lionking.ini next to itself, where the game reads them, and starts the game:
 * IlReLeone.exe (release folder) or lionking.exe (build folder) from the same folder. The game
 * data is found as GIOCA.bat / run.bat do: DATA next to the game, in ..\ or ..\..\ (the engine
 * looks there by itself), else the original CD in the drives D: to Z:.
 * The texts are in Italian, as the game. */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE /* -municode defines it too */
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
	ID_FULLSCREEN = 100,
	ID_SCALE,
	ID_ASPECT,
	ID_FILTER,
	ID_FADES,
	ID_PLAY,
	ID_SAVE,
	ID_QUIT,
};

/* values written to lionking.ini, in the order of the combo box entries */
static const char *const scale_values[] = {"1", "2", "3", "4"};
static const char *const aspect_values[] = {"4:3", "stretch", "integer"};
static const char *const filter_values[] = {"sharp", "smooth"};
static const char *const fades_values[] = {"palette", "instant"};

static const wchar_t *const scale_names[] = {L"1× (640×480)", L"2× (1280×960)", L"3× (1920×1440)", L"4× (2560×1920)"};
static const wchar_t *const aspect_names[] = {L"4:3 con bande nere", L"Riempi lo schermo (deforma)",
	L"Solo fattori interi (pixel perfetti)"};
static const wchar_t *const filter_names[] = {L"Nitido", L"Morbido"};
static const wchar_t *const fades_names[] = {L"Palette (come a 256 colori)", L"Istantanee (come su Windows moderni)"};

typedef struct {
	int fullscreen, scale, aspect, filter, fades; /* combo indexes */
} Options;

static wchar_t base_dir[MAX_PATH];
static HWND controls[ID_QUIT + 1];
static HFONT font;

static int index_of(const char *const *values, int n, const char *v) {
	for (int i = 0; i < n; i++)
		if (!strcmp(values[i], v))
			return i;
	return -1;
}

static void ini_path(wchar_t *out) {
	swprintf(out, MAX_PATH, L"%lslionking.ini", base_dir);
}

/* Reads lionking.ini (same syntax as engine/main.c read_ini); unknown lines are ignored. */
static void load_options(Options *o) {
	wchar_t path[MAX_PATH];
	char line[256];
	ini_path(path);
	FILE *f = _wfopen(path, L"r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char key[64], v[64];
		if (sscanf(line, " %63[a-z] = %63[^ \t\r\n;#]", key, v) != 2)
			continue;
		int i;
		if (!strcmp(key, "fullscreen"))
			o->fullscreen = atoi(v) != 0 || !strcmp(v, "yes") || !strcmp(v, "si") || !strcmp(v, "true");
		else if (!strcmp(key, "scale") && (i = index_of(scale_values, 4, v)) >= 0)
			o->scale = i;
		else if (!strcmp(key, "aspect") && (i = index_of(aspect_values, 3, v)) >= 0)
			o->aspect = i;
		else if (!strcmp(key, "aspect") && !strcmp(v, "intera"))
			o->aspect = 2;
		else if (!strcmp(key, "filter") && (i = index_of(filter_values, 2, v)) >= 0)
			o->filter = i;
		else if (!strcmp(key, "filter"))
			o->filter = !strcmp(v, "morbido");
		else if (!strcmp(key, "fades") && (i = index_of(fades_values, 2, v)) >= 0)
			o->fades = i;
		else if (!strcmp(key, "fades"))
			o->fades = !strcmp(v, "istantanee");
	}
	fclose(f);
}

static int save_options(const Options *o) {
	wchar_t path[MAX_PATH];
	ini_path(path);
	FILE *f = _wfopen(path, L"w");
	if (!f)
		return 0;
	fprintf(f, "; Il Re Leone (port SDL2): opzioni scritte da launcher.exe.\n"
		   "; Le opzioni sulla riga di comando del gioco valgono piu' di queste.\n"
		   "fullscreen=%d\nscale=%s\naspect=%s\nfilter=%s\nfades=%s\n",
		o->fullscreen, scale_values[o->scale], aspect_values[o->aspect], filter_values[o->filter],
		fades_values[o->fades]);
	return fclose(f) == 0;
}

static void read_controls(Options *o) {
	o->fullscreen = SendMessageW(controls[ID_FULLSCREEN], BM_GETCHECK, 0, 0) == BST_CHECKED;
	o->scale = (int)SendMessageW(controls[ID_SCALE], CB_GETCURSEL, 0, 0);
	o->aspect = (int)SendMessageW(controls[ID_ASPECT], CB_GETCURSEL, 0, 0);
	o->filter = (int)SendMessageW(controls[ID_FILTER], CB_GETCURSEL, 0, 0);
	o->fades = (int)SendMessageW(controls[ID_FADES], CB_GETCURSEL, 0, 0);
}

static int exists(const wchar_t *p) {
	return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

/* Starts the game; 0 with a message on failure. */
static int play(HWND w) {
	wchar_t game[MAX_PATH], cmd[3 * MAX_PATH], p[MAX_PATH];
	swprintf(game, MAX_PATH, L"%lsIlReLeone.exe", base_dir);
	if (!exists(game))
		swprintf(game, MAX_PATH, L"%lslionking.exe", base_dir);
	if (!exists(game)) {
		MessageBoxW(w, L"IlReLeone.exe non trovato accanto a launcher.exe.", L"Il Re Leone", MB_ICONERROR);
		return 0;
	}
	swprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%ls\"", game);
	/* the data folders the engine finds by itself (engine/main.c find_data) */
	static const wchar_t *const rel[] = {L"DATA", L"..\\DATA", L"..\\..\\DATA"};
	int found = 0;
	for (int i = 0; i < 3 && !found; i++) {
		swprintf(p, MAX_PATH, L"%ls%ls\\BOOT.STM", base_dir, rel[i]);
		found = exists(p);
	}
	for (wchar_t d = L'D'; d <= L'Z' && !found; d++) {
		wchar_t exe[16];
		swprintf(p, MAX_PATH, L"%lc:\\DATA\\BOOT.STM", d);
		swprintf(exe, 16, L"%lc:\\LIONKING.EXE", d);
		if (exists(p) && exists(exe)) {
			swprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%ls\" --data %lc:\\DATA --exe %lc:\\LIONKING.EXE", game, d, d);
			found = 1;
		}
	}
	if (!found) {
		MessageBoxW(w,
			L"Dati del gioco non trovati.\n\nCopia la cartella DATA e il file LIONKING.EXE del CD accanto "
			L"a IlReLeone.exe (vedi LEGGIMI.txt), oppure inserisci il CD originale.",
			L"Il Re Leone", MB_ICONERROR);
		return 0;
	}
	STARTUPINFOW si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(game, cmd, NULL, NULL, FALSE, 0, NULL, base_dir, &si, &pi)) {
		MessageBoxW(w, L"Impossibile avviare il gioco.", L"Il Re Leone", MB_ICONERROR);
		return 0;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 1;
}

#define MARGIN 14
#define LABEL_W 150
#define FIELD_W 300
#define ROW_H 24
#define NOTE_H 34
#define WIN_W (MARGIN * 2 + LABEL_W + FIELD_W)

static HWND add(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id) {
	HWND c = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id,
		GetModuleHandleW(NULL), NULL);
	SendMessageW(c, WM_SETFONT, (WPARAM)font, FALSE);
	if (id > 0 && id <= ID_QUIT)
		controls[id] = c;
	return c;
}

/* One option: label, control (combo box with the given entries, or check box) and a line
 * that explains it. Returns the y of the next row. */
static int add_row(HWND w, int y, const wchar_t *label, int id, const wchar_t *const *names, int n, int sel,
	const wchar_t *note) {
	add(w, L"STATIC", label, 0, MARGIN, y + 4, LABEL_W, ROW_H, -1);
	if (names) {
		HWND c = add(w, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, MARGIN + LABEL_W, y, FIELD_W,
			ROW_H * 6, id);
		for (int i = 0; i < n; i++)
			SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)names[i]);
		SendMessageW(c, CB_SETCURSEL, (WPARAM)sel, 0);
	} else {
		HWND c = add(w, L"BUTTON", L"Sì", BS_AUTOCHECKBOX | WS_TABSTOP, MARGIN + LABEL_W, y, FIELD_W, ROW_H, id);
		SendMessageW(c, BM_SETCHECK, sel ? BST_CHECKED : BST_UNCHECKED, 0);
	}
	add(w, L"STATIC", note, SS_LEFT, MARGIN + LABEL_W, y + ROW_H + 2, FIELD_W, NOTE_H, -1);
	return y + ROW_H + NOTE_H + 10;
}

static int create_controls(HWND w) {
	Options o = {0, 1, 0, 0, 0};
	load_options(&o);
	int y = MARGIN;
	add(w, L"STATIC", L"Il Re Leone - Libro Animato Interattivo: opzioni del port. La grafica del gioco resta "
		L"sempre 640×480, come l'originale.", SS_LEFT, MARGIN, y, WIN_W - 2 * MARGIN, 34, -1);
	y += 44;
	y = add_row(w, y, L"Schermo intero", ID_FULLSCREEN, NULL, 0, o.fullscreen,
		L"Occupa tutto lo schermo. Alt+Invio passa da finestra a schermo intero anche durante il gioco.");
	y = add_row(w, y, L"Dimensione finestra", ID_SCALE, scale_names, 4, o.scale,
		L"Grandezza della finestra all'avvio (si può anche allargare col mouse).");
	y = add_row(w, y, L"Proporzioni", ID_ASPECT, aspect_names, 3, o.aspect,
		L"4:3 come i monitor del 1995; \"riempi\" allarga l'immagine; \"interi\" ingrandisce solo di 2×, 3×...");
	y = add_row(w, y, L"Filtro", ID_FILTER, filter_names, 2, o.filter,
		L"Nitido: pixel netti come l'originale. Morbido: bordi sfumati quando l'immagine è ingrandita.");
	y = add_row(w, y, L"Dissolvenze", ID_FADES, fades_names, 2, o.fades,
		L"Palette: il nero fra le schermate dura mezzo secondo. Istantanee: cambio subito, come l'originale oggi.");
	int bw = 96, bx = WIN_W - MARGIN - 3 * bw - 16;
	add(w, L"BUTTON", L"Gioca", BS_DEFPUSHBUTTON | WS_TABSTOP, bx, y, bw, 28, ID_PLAY);
	add(w, L"BUTTON", L"Salva", WS_TABSTOP, bx + bw + 8, y, bw, 28, ID_SAVE);
	add(w, L"BUTTON", L"Esci", WS_TABSTOP, bx + 2 * (bw + 8), y, bw, 28, ID_QUIT);
	return y + 28 + MARGIN;
}

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_COMMAND: {
		Options o;
		switch (LOWORD(wp)) {
		case ID_PLAY:
		case IDOK: /* Enter (IsDialogMessage) */
			read_controls(&o);
			if (!save_options(&o))
				MessageBoxW(w, L"Impossibile scrivere lionking.ini: il gioco parte con le opzioni precedenti.",
					L"Il Re Leone", MB_ICONWARNING);
			if (play(w))
				DestroyWindow(w);
			return 0;
		case ID_SAVE:
			read_controls(&o);
			if (!save_options(&o))
				MessageBoxW(w, L"Impossibile scrivere lionking.ini accanto a launcher.exe.", L"Il Re Leone",
					MB_ICONERROR);
			return 0;
		case ID_QUIT:
		case IDCANCEL:
			DestroyWindow(w);
			return 0;
		}
		break;
	}
	case WM_CTLCOLORSTATIC:
		SetBkMode((HDC)wp, TRANSPARENT);
		return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show) {
	(void)prev;
	(void)args;
	GetModuleFileNameW(NULL, base_dir, MAX_PATH);
	wchar_t *slash = wcsrchr(base_dir, L'\\');
	if (slash)
		slash[1] = 0;

	NONCLIENTMETRICSW ncm;
	memset(&ncm, 0, sizeof(ncm));
	ncm.cbSize = sizeof(ncm);
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	font = CreateFontIndirectW(&ncm.lfMessageFont);

	WNDCLASSW wc = {0};
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = inst;
	wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
	wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
	wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
	wc.lpszClassName = L"IlReLeoneLauncher";
	RegisterClassW(&wc);
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	HWND w = CreateWindowW(wc.lpszClassName, L"Il Re Leone - Opzioni", style, CW_USEDEFAULT, CW_USEDEFAULT, WIN_W,
		400, NULL, NULL, inst, NULL);
	int h = create_controls(w);
	RECT r = {0, 0, WIN_W, h};
	AdjustWindowRect(&r, style, FALSE);
	SetWindowPos(w, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
	ShowWindow(w, show);
	SetFocus(controls[ID_PLAY]);

	MSG m;
	while (GetMessageW(&m, NULL, 0, 0) > 0) {
		if (IsDialogMessageW(w, &m))
			continue;
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	DeleteObject(font);
	return 0;
}
