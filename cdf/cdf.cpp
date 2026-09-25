
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ------------------------------------------------------------
// フォルダ情報
// ------------------------------------------------------------
struct Folder {
	fs::path path;
	std::wstring name;
	bool parent = false;
};

// ------------------------------------------------------------
// フォルダ一覧を取得
// ------------------------------------------------------------
std::vector<Folder> GetFolders(const fs::path& dir)
{
	std::vector<Folder> folders;

	// 親ディレクトリ
	fs::path parent = dir.parent_path();

	if (!parent.empty() && parent != dir) {
		folders.push_back({
			parent,
			L"<..>",
			true
			});
	}

	const size_t first = folders.size();

	// 子ディレクトリのみ取得
	for (const auto& entry :
		fs::directory_iterator(dir)) {

		std::error_code ec;

		if (entry.is_directory(ec) && !ec) {
			folders.push_back({
				entry.path(),
				entry.path().filename().wstring(),
				false
				});
		}
	}

	// 親ディレクトリを先頭に固定して名前順にソート
	std::sort(
		folders.begin() + first,
		folders.end(),
		[](const Folder& a, const Folder& b) {
			int result = CompareStringOrdinal(
				a.name.c_str(), -1,
				b.name.c_str(), -1,
				TRUE
			);

			if (result == CSTR_EQUAL)
				return a.name < b.name;

			return result == CSTR_LESS_THAN;
		}
	);

	return folders;
}

// ------------------------------------------------------------
// ドライブ一覧を取得
// ------------------------------------------------------------
std::vector<Folder> GetDrives()
{
	std::vector<Folder> drives;

	DWORD mask = GetLogicalDrives();

	for (int i = 0; i < 26; ++i) {
		if (!(mask & (1u << i)))
			continue;

		wchar_t letter =
			static_cast<wchar_t>(L'A' + i);

		std::wstring root;
		root += letter;
		root += L":\\";

		if (GetDriveTypeW(root.c_str()) ==
			DRIVE_NO_ROOT_DIR)
			continue;

		drives.push_back({
			fs::path(root),
			root,
			false
			});
	}

	return drives;
}

// ------------------------------------------------------------
// 全角文字を考慮した表示幅
// ------------------------------------------------------------
int CharacterWidth(wchar_t c)
{
	if ((c >= 0x1100 && c <= 0x115F) ||
		(c >= 0x2E80 && c <= 0xA4CF) ||
		(c >= 0xAC00 && c <= 0xD7A3) ||
		(c >= 0xF900 && c <= 0xFAFF) ||
		(c >= 0xFE10 && c <= 0xFE6F) ||
		(c >= 0xFF01 && c <= 0xFF60))
		return 2;

	return 1;
}

// ------------------------------------------------------------
// コンソール幅に合わせて文字列を切り詰める
// ------------------------------------------------------------
std::wstring FitText(
	const std::wstring& text,
	int width)
{
	if (width <= 0)
		return L"";

	std::wstring result;
	int used = 0;

	for (wchar_t c : text) {
		int w = CharacterWidth(c);

		if (used + w > width) {
			if (used < width)
				result += L"…";
			break;
		}

		result += c;
		used += w;
	}

	return result;
}

// ------------------------------------------------------------
// 指定行を描画
// ------------------------------------------------------------
void DrawLine(
	HANDLE out,
	SHORT x,
	SHORT y,
	SHORT width,
	const std::wstring& text,
	WORD color)
{
	if (width <= 0)
		return;

	COORD pos = { x, y };
	DWORD written = 0;

	FillConsoleOutputCharacterW(
		out,
		L' ',
		width,
		pos,
		&written
	);

	FillConsoleOutputAttribute(
		out,
		color,
		width,
		pos,
		&written
	);

	SetConsoleCursorPosition(out, pos);
	SetConsoleTextAttribute(out, color);

	std::wstring clipped =
		FitText(text, width);

	if (!clipped.empty()) {
		WriteConsoleW(
			out,
			clipped.c_str(),
			static_cast<DWORD>(clipped.size()),
			&written,
			nullptr
		);
	}
}

// ------------------------------------------------------------
// 一覧画面の描画
// ------------------------------------------------------------
void DrawScreen(
	HANDLE out,
	const fs::path& current,
	const std::vector<Folder>& folders,
	size_t selected,
	size_t& scroll,
	const std::wstring& message,
	bool driveMode = false)
{
	CONSOLE_SCREEN_BUFFER_INFO info{};

	if (!GetConsoleScreenBufferInfo(out, &info))
		return;

	SHORT x = info.srWindow.Left;
	SHORT top = info.srWindow.Top;

	SHORT width = static_cast<SHORT>(
		info.srWindow.Right -
		info.srWindow.Left + 1
		);

	SHORT height = static_cast<SHORT>(
		info.srWindow.Bottom -
		info.srWindow.Top + 1
		);

	if (height < 5)
		return;

	const WORD normal =
		FOREGROUND_RED |
		FOREGROUND_GREEN |
		FOREGROUND_BLUE;

	const WORD highlight =
		BACKGROUND_BLUE |
		FOREGROUND_RED |
		FOREGROUND_GREEN |
		FOREGROUND_BLUE |
		FOREGROUND_INTENSITY;

	const WORD title =
		FOREGROUND_GREEN |
		FOREGROUND_BLUE |
		FOREGROUND_INTENSITY;

	// タイトル
	DrawLine(
		out, x, top, width,
		driveMode
		? L"ドライブ選択"
		: L"フォルダ: " + current.wstring(),
		title
	);

	// 操作説明
	DrawLine(
		out, x, top + 1, width,
		driveMode
		? L"↑↓:選択 Enter:開く Space/Q:決定 Esc:戻る"
		: L"↑↓:選択 Enter:開く Space/Q:決定 D:ドライブ Esc:戻る",
		normal
	);

	// 区切り線
	DrawLine(
		out, x, top + 2, width,
		std::wstring(width, L'-'),
		normal
	);

	// 一覧表示可能行数
	const size_t visible =
		static_cast<size_t>(height - 4);

	// スクロール位置を調整
	if (selected < scroll)
		scroll = selected;

	if (selected >= scroll + visible)
		scroll = selected - visible + 1;

	// 一覧描画
	for (size_t row = 0; row < visible; ++row) {
		size_t index = scroll + row;

		SHORT y = static_cast<SHORT>(
			top + 3 + row
			);

		if (index >= folders.size()) {
			DrawLine(
				out, x, y, width,
				folders.empty() && row == 0
				? L"（項目がありません）"
				: L"",
				normal
			);
			continue;
		}

		std::wstring text =
			index == selected ? L"> " : L"  ";

		text += folders[index].name;

		DrawLine(
			out, x, y, width,
			text,
			index == selected
			? highlight
			: normal
		);
	}

	// ステータス行
	std::wstring status = message;

	if (status.empty() && !folders.empty()) {
		status =
			std::to_wstring(selected + 1) +
			L" / " +
			std::to_wstring(folders.size());
	}

	DrawLine(
		out,
		x,
		static_cast<SHORT>(top + height - 1),
		width,
		status,
		normal
	);
}

// ------------------------------------------------------------
// クリップボードへパスをコピー
//
// 例：
// D:\My Documents
//      ↓
// "D:\My Documents"
// ------------------------------------------------------------
bool CopyPathToClipboard(const fs::path& path)
{
	// パスをダブルクォーテーションで囲む
	std::wstring text =
		L"\"" + path.wstring() + L"\"";

	// NULL終端を含めてメモリを確保
	SIZE_T bytes =
		(text.size() + 1) * sizeof(wchar_t);

	HGLOBAL memory =
		GlobalAlloc(GMEM_MOVEABLE, bytes);

	if (!memory)
		return false;

	void* buffer = GlobalLock(memory);

	if (!buffer) {
		GlobalFree(memory);
		return false;
	}

	std::memcpy(
		buffer,
		text.c_str(),
		bytes
	);

	GlobalUnlock(memory);

	// クリップボードを開く
	if (!OpenClipboard(nullptr)) {
		GlobalFree(memory);
		return false;
	}

	// 既存のクリップボード内容を消去
	if (!EmptyClipboard()) {
		CloseClipboard();
		GlobalFree(memory);
		return false;
	}

	// Unicode文字列を設定
	if (!SetClipboardData(
		CF_UNICODETEXT,
		memory)) {

		CloseClipboard();
		GlobalFree(memory);
		return false;
	}

	// 成功後のメモリ管理はWindowsが行う
	CloseClipboard();

	return true;
}

// ------------------------------------------------------------
// 選択パスをUTF-8で標準出力へ返す
// PowerShell連携用
// ------------------------------------------------------------
bool OutputPath(const fs::path& path)
{
	std::wstring wide = path.wstring();

	int size = WideCharToMultiByte(
		CP_UTF8,
		0,
		wide.c_str(),
		static_cast<int>(wide.size()),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	if (size <= 0)
		return false;

	std::string utf8(size, '\0');

	WideCharToMultiByte(
		CP_UTF8,
		0,
		wide.c_str(),
		static_cast<int>(wide.size()),
		utf8.data(),
		size,
		nullptr,
		nullptr
	);

	utf8 += '\n';

	DWORD written = 0;

	return WriteFile(
		GetStdHandle(STD_OUTPUT_HANDLE),
		utf8.data(),
		static_cast<DWORD>(utf8.size()),
		&written,
		nullptr
	) != 0 && written == utf8.size();
}

// ------------------------------------------------------------
// フォルダの決定処理
// カレント変更 → クリップボードへコピー
//             → 標準出力へ出力
// ------------------------------------------------------------
bool FinishSelection(
	HANDLE out,
	const fs::path& target,
	std::wstring& message,
	int& result)
{
	std::error_code ec;

	fs::current_path(target, ec);

	if (ec) {
		message = L"フォルダへ移動できません。";
		return false;
	}

	if (!CopyPathToClipboard(target)) {
		message = L"クリップボードへのコピーに失敗しました。";
		return false;
	}

	// パスの出力前に改行
	DWORD written = 0;
	WriteConsoleW(
		out,
		L"\r\n",
		2,
		&written,
		nullptr
	);

	result = OutputPath(target) ? 0 : 2;

	return true;
}

// ------------------------------------------------------------
// ドライブ選択
// ------------------------------------------------------------
enum class DriveAction {
	Cancel,
	Open,
	Finish
};

DriveAction SelectDrive(
	HANDLE in,
	HANDLE out,
	fs::path& target)
{
	auto drives = GetDrives();

	if (drives.empty())
		return DriveAction::Cancel;

	size_t selected = 0;
	size_t scroll = 0;

	while (true) {
		DrawScreen(
			out,
			fs::path(),
			drives,
			selected,
			scroll,
			L"",
			true
		);

		INPUT_RECORD event{};
		DWORD count = 0;

		if (!ReadConsoleInputW(
			in, &event, 1, &count))
			return DriveAction::Cancel;

		if (event.EventType != KEY_EVENT ||
			!event.Event.KeyEvent.bKeyDown)
			continue;

		WORD key =
			event.Event.KeyEvent.wVirtualKeyCode;

		switch (key) {
		case VK_UP:
			if (selected > 0)
				--selected;
			break;

		case VK_DOWN:
			if (selected + 1 < drives.size())
				++selected;
			break;

		case VK_HOME:
			selected = 0;
			break;

		case VK_END:
			selected = drives.size() - 1;
			break;

			// Enter: ドライブを開く
		case VK_RETURN:
			target = drives[selected].path;
			return DriveAction::Open;

			// Space / Q: ドライブを決定
		case VK_SPACE:
		case 'Q':
			target = drives[selected].path;
			return DriveAction::Finish;

			// Esc / D: 元の一覧へ戻る
		case VK_ESCAPE:
		case 'D':
			return DriveAction::Cancel;
		}
	}
}

// ------------------------------------------------------------
// メイン
// ------------------------------------------------------------
int wmain(int argc, wchar_t* argv[])
{
	// 標準出力をリダイレクトしていても
	// 一覧画面はコンソールへ直接表示する
	HANDLE out = CreateFileW(
		L"CONOUT$",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr
	);

	HANDLE in = CreateFileW(
		L"CONIN$",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr
	);

	if (out == INVALID_HANDLE_VALUE ||
		in == INVALID_HANDLE_VALUE) {

		std::cerr << "Console is unavailable.\n";

		if (out != INVALID_HANDLE_VALUE)
			CloseHandle(out);

		if (in != INVALID_HANDLE_VALUE)
			CloseHandle(in);

		return 2;
	}

	UINT oldCodePage = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);

	// 終了コード
	// 0: 決定
	// 1: キャンセル
	// 2: エラー
	int result = 1;

	// カーソルを非表示にする
	CONSOLE_CURSOR_INFO oldCursor{};

	bool hasCursor =
		GetConsoleCursorInfo(out, &oldCursor) != 0;

	if (hasCursor) {
		CONSOLE_CURSOR_INFO hidden = oldCursor;
		hidden.bVisible = FALSE;

		SetConsoleCursorInfo(out, &hidden);
	}

	try {
		// 引数なしならカレントディレクトリ
		fs::path current =
			argc >= 2
			? fs::absolute(fs::path(argv[1]))
			: fs::current_path();

		current = current.lexically_normal();

		if (!fs::is_directory(current)) {
			std::wcerr
				<< L"フォルダが存在しません: "
				<< current.wstring()
				<< L'\n';

			result = 2;
		}
		else {
			auto folders = GetFolders(current);

			// Escで戻るための履歴
			std::vector<fs::path> history;

			size_t selected = 0;
			size_t scroll = 0;

			std::wstring message;

			bool running = true;

			while (running) {

				DrawScreen(
					out,
					current,
					folders,
					selected,
					scroll,
					message
				);

				INPUT_RECORD event{};
				DWORD count = 0;

				if (!ReadConsoleInputW(
					in, &event, 1, &count)) {
					result = 2;
					break;
				}

				if (event.EventType != KEY_EVENT ||
					!event.Event.KeyEvent.bKeyDown)
					continue;

				WORD key =
					event.Event.KeyEvent.wVirtualKeyCode;

				message.clear();

				switch (key) {

					// ----------------------------------------
					// カーソル移動
					// ----------------------------------------
				case VK_UP:
					if (selected > 0)
						--selected;
					break;

				case VK_DOWN:
					if (selected + 1 < folders.size())
						++selected;
					break;

				case VK_HOME:
					selected = 0;
					break;

				case VK_END:
					if (!folders.empty())
						selected = folders.size() - 1;
					break;

					// ----------------------------------------
					// Enter: フォルダを開く
					// ----------------------------------------
				case VK_RETURN:
				{
					if (folders.empty())
						break;

					try {
						fs::path next =
							folders[selected].path;

						auto nextFolders =
							GetFolders(next);

						history.push_back(current);

						current = next;
						folders = std::move(nextFolders);

						selected = 0;
						scroll = 0;
					}
					catch (const fs::filesystem_error&) {
						message =
							L"フォルダを開けません。";
					}

					break;
				}

				// ----------------------------------------
				// Space / Q:
				// パスをクリップボードへコピーして終了
				// ----------------------------------------
				case VK_SPACE:
				case 'Q':
				{
					if (folders.empty())
						break;

					fs::path target =
						folders[selected].path;

					if (FinishSelection(
						out,
						target,
						message,
						result)) {

						running = false;
					}

					break;
				}

				// ----------------------------------------
				// D: ドライブ選択
				// ----------------------------------------
				case 'D':
				{
					fs::path drive;

					DriveAction action =
						SelectDrive(in, out, drive);

					if (action == DriveAction::Cancel)
						break;

					// Enter: 選択したドライブを開く
					if (action == DriveAction::Open) {
						try {
							auto nextFolders =
								GetFolders(drive);

							history.push_back(current);

							current = drive;
							folders =
								std::move(nextFolders);

							selected = 0;
							scroll = 0;
						}
						catch (const fs::filesystem_error&) {
							message =
								L"ドライブを開けません。";
						}
					}

					// Space / Q:
					// ドライブを決定してコピー・終了
					else if (
						action == DriveAction::Finish) {

						if (FinishSelection(
							out,
							drive,
							message,
							result)) {

							running = false;
						}
					}

					break;
				}

				// ----------------------------------------
				// Esc: 元のフォルダへ戻る
				// ----------------------------------------
				case VK_ESCAPE:
				{
					if (history.empty()) {
						// 最初の画面なら終了
						result = 1;
						running = false;
					}
					else {
						try {
							fs::path previous =
								history.back();

							auto previousFolders =
								GetFolders(previous);

							history.pop_back();

							current = previous;
							folders =
								std::move(previousFolders);

							selected = 0;
							scroll = 0;
						}
						catch (const fs::filesystem_error&) {
							message =
								L"元のフォルダへ戻れません。";
						}
					}

					break;
				}

				} // switch
			} // while
		}
	}
	catch (const fs::filesystem_error& e) {
		std::cerr << e.what() << '\n';
		result = 2;
	}

	// コンソール設定を元に戻す
	if (hasCursor)
		SetConsoleCursorInfo(out, &oldCursor);

	DWORD written = 0;

	WriteConsoleW(
		out,
		L"\r\n",
		2,
		&written,
		nullptr
	);

	if (oldCodePage != 0)
		SetConsoleOutputCP(oldCodePage);

	CloseHandle(in);
	CloseHandle(out);

	return result;
}