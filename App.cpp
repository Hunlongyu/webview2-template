#include "framework.h"
#include "app.h" ///记得修改
#include <windows.h>
#include <strsafe.h>
#include <io.h>
#include <regex>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include <wrl.h>
#include <wil/com.h>
#include <AccCtrl.h>
#include <AclAPI.h>
#include <fstream>
#include <atlstr.h>

#include "webView2.h"
#include "includes/json.hpp"
using namespace Microsoft::WRL;
using json = nlohmann::json;

// Global variables
static TCHAR szWindowClass[] = _T("DesktopApp");
TCHAR szTitle[] = _T("title");
std::string szIcon = "app.ico";
int szWidth = 1080;
int szHeight = 720;
std::string szLaunch = "index.html";

HINSTANCE hInst;
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static wil::com_ptr<ICoreWebView2Controller> webviewController;
static wil::com_ptr<ICoreWebView2> webviewWindow;

std::wstring GetLocalPath(std::wstring relativePath, bool keep_exe_path)
{
	WCHAR rawPath[MAX_PATH];
	GetModuleFileNameW(hInst, rawPath, MAX_PATH);
	std::wstring path(rawPath);
	if (keep_exe_path) {
		path.append(relativePath);
	}
	else {
		std::size_t index = path.find_last_of(L"\\") + 1;
		path.replace(index, path.length(), relativePath);
	}
	return path;
}

std::wstring GetLocalUri(std::wstring relativePath, bool useVirtualHostName)
{
	if (useVirtualHostName) {
		const std::wstring localFileRootUrl = L"https://appassets.example/";
		return localFileRootUrl + regex_replace(relativePath, std::wregex(L"\\\\"), L"/");
	}
	else {
		std::wstring path = GetLocalPath(L"assets\\" + relativePath, false);
		wil::com_ptr<IUri> uri;
		CreateUri(path.c_str(), Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME, 0, &uri);
		wil::unique_bstr uriBstr;
		uri->GetAbsoluteUri(&uriBstr);
		return std::wstring(uriBstr.get());
	}
}

// 获取配置文件的数据
void GetConfigJsonFile()
{
	std::wstring path = GetLocalPath(L"config.json", false);
	json config;
	std::ifstream jFile(path);
	if (!jFile.is_open()) {
		MessageBox(NULL, _T("未找到 config.json 文件!"), _T("Error!"), NULL);
	}
	try {
		jFile >> config;
	}
	catch (json::exception& e) {
		auto error = e.what();
		MessageBox(NULL, _T("config.json 数据格式不对，请检查后重试。"), _T("Warning!"), NULL);
		return;
	}

	auto it_windowTitle = config.find("windowTitle");
	if (it_windowTitle != config.end()) {
		std::string title = config.at("windowTitle");
		if (!title.empty()) {
			_tcscpy_s(szTitle, CA2T(title.c_str()));
		}
	}

	auto it_icon = config.find("icon");
	if (it_icon != config.end()) {
		std::string icon = config.at("icon");
		if (!icon.empty()) {
			szIcon = icon;
		}
	}

	auto it_width = config.find("width");
	if (it_width != config.end()) {
		int width = config.at("width");
		szWidth = width;
	}

	auto it_height = config.find("height");
	if (it_height != config.end()) {
		int height = config.at("height");
		szHeight = height;
	}

	auto it_launch = config.find("launch");
	if (it_launch != config.end()) {
		std::string launch = config.at("launch");
		if (!launch.empty()) {
			szLaunch = launch;
		}
	}

	jFile.close();
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	GetConfigJsonFile();
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	if (!RegisterClassEx(&wcex)) {
		MessageBox(NULL, _T("Call to RegisterClassEx failed!"), _T("Windows Desktop Guided Tour"), NULL);
		return 1;
	}

	hInst = hInstance;
	HWND hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, szWidth, szHeight, NULL, NULL, hInstance, NULL);

	if (!hWnd) {
		MessageBox(NULL, _T("Call to CreateWindow failed!"), _T("Windows Desktop Guided Tour"), NULL);
		return 1;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	// <-- WebView2 sample code starts here -->

	HRESULT res = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[hWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
				//MessageBoxA(hWnd, "createView", "", NULL);
				// Create a CoreWebView2Controller and get the associated CoreWebView2 whose parent is the main window hWnd
				env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
					[hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
						if (controller != nullptr) {
							webviewController = controller;
							webviewController->get_CoreWebView2(&webviewWindow);
						}

						ICoreWebView2Settings* Settings;
						webviewWindow->get_Settings(&Settings);
						Settings->put_IsScriptEnabled(TRUE);
						Settings->put_AreDefaultScriptDialogsEnabled(TRUE);
						Settings->put_AreDefaultContextMenusEnabled(FALSE);
						Settings->put_IsWebMessageEnabled(TRUE);

						RECT bounds;
						GetClientRect(hWnd, &bounds);
						webviewController->put_Bounds(bounds);

						static constexpr WCHAR c_samplePath[] = L"index.html";
						std::wstring m_sampleUri = GetLocalUri(c_samplePath, false);
						HRESULT res = webviewWindow->Navigate(m_sampleUri.c_str());

						std::string sres = std::to_string(res).c_str();
						// Step 4 - Navigation events

						// Step 5 - Scripting

						// Step 6 - Communication between host and web content

						return S_OK;
					}).Get());
				return S_OK;
			}).Get());
	// <-- WebView2 sample code ends here -->

	// Main message loop:
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_SIZE:
		if (webviewController != nullptr) {
			RECT bounds;
			GetClientRect(hWnd, &bounds);
			webviewController->put_Bounds(bounds);
		};
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}
	return 0;
}
