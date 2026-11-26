#pragma comment(lib, "d3d11.lib")

//#include <mm/core/graphicsengine/graphicsengine.h>
#include "plugin.h"
#include "mm/core/graphics/graphicsengine.h"
#include "imgui_detour_dx11.h"

ImGui::DetourDX11::present  ImGui::DetourDX11::p_present = 0;
ImGui::DetourDX11::present	ImGui::DetourDX11::p_present_target = 0;

std::function<void(bool)> ImGui::DetourDX11::_onInit;
std::function<void()> ImGui::DetourDX11::_onRender;
std::function<void()> ImGui::DetourDX11::_onShutdown;

WNDPROC ImGui::DetourDX11::oWndProc = nullptr;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// The window procedure runs on the window's thread, the render setups are created and destroyed on another
// one: a WM_MOUSEMOVE that came in while the Win32 backend was being shut down wrote through its freed data
// (crash on closing the game, 2026-10-25 on GOG, steamtest6 on Steam). Backend init/shutdown and the frame
// hold this lock exclusively / shared; the window procedure only feeds ImGui when it gets it at once.
static SRWLOCK s_imguiLock = SRWLOCK_INIT;

LRESULT __stdcall ImGui::DetourDX11::WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (TryAcquireSRWLockShared(&s_imguiLock)) {
		LRESULT handled = ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
		ReleaseSRWLockShared(&s_imguiLock);
		if (handled)
			return true;
	}

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

DEFHOOK(void, Graphics__Flip, (Graphics::HDevice_t* device)) {

	if (device->m_Context->m_Mutex)
		EnterCriticalSection((LPCRITICAL_SECTION)device->m_Context->m_Mutex);

	bool locked = TryAcquireSRWLockShared(&s_imguiLock) != 0;
	if (locked && ImGui::GetCurrentContext()) {
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();

		ImGui::NewFrame();

		if (ImGui::DetourDX11::_onRender)
			ImGui::DetourDX11::_onRender();

		ImGui::EndFrame();
		ImGui::Render();

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}
	if (locked)
		ReleaseSRWLockShared(&s_imguiLock);

	if (device->m_Context->m_Mutex)
		LeaveCriticalSection((LPCRITICAL_SECTION)device->m_Context->m_Mutex);

	return Graphics__Flip_orig(device);
}

DEFHOOK(void, DestroyRenderSetups, (NGraphicsEngine::CGraphicsEngine* thiz)) {

	AcquireSRWLockExclusive(&s_imguiLock);
	if (ImGui::DetourDX11::_onShutdown)
		ImGui::DetourDX11::_onShutdown();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	ReleaseSRWLockExclusive(&s_imguiLock);

	return DestroyRenderSetups_orig(thiz);
}

DEFHOOK(void, CreateRenderSetups, (NGraphicsEngine::CGraphicsEngine* thiz)) {
	CreateRenderSetups_orig(thiz);

	static bool reInit = false;

	DXGI_SWAP_CHAIN_DESC sd;
	thiz->m_GraphicsDevice->m_SwapChain->GetDesc(&sd);

	AcquireSRWLockExclusive(&s_imguiLock);
	bool needCallInit = false;
	if (!ImGui::GetCurrentContext()) {
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
		if(!ImGui::DetourDX11::oWndProc)
			ImGui::DetourDX11::oWndProc = (WNDPROC)SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)ImGui::DetourDX11::WndProc);
		needCallInit = true;
	}

	ImGui_ImplWin32_Init(sd.OutputWindow);
	ImGui_ImplDX11_Init(thiz->m_GraphicsDevice->m_D3DDevice, thiz->m_GraphicsDevice->m_Context->m_Context);

	if (needCallInit && ImGui::DetourDX11::_onInit) {
		ImGui::DetourDX11::_onInit(reInit);
		reInit = true;
	}
	ReleaseSRWLockExclusive(&s_imguiLock);
}

void ImGui::DetourDX11::Install(uintptr_t flipAddr, uintptr_t createRender, uintptr_t destroyRender,
	std::function<void(bool)> onInit,
	std::function<void()> onRender,
	std::function<void()> onShutdown)
{
	_onInit = onInit;
	_onRender = onRender;
	_onShutdown = onShutdown;

	HookMgr::Install(flipAddr, Graphics__Flip_hook, Graphics__Flip_orig); // ?Flip@Graphics@@YA?AW4EResult@1@PEAUHDevice_t@1@@Z
	HookMgr::Install(createRender, CreateRenderSetups_hook, CreateRenderSetups_orig); // ?CreateRenderSetups@CGraphicsEngine@NGraphicsEngine@@AEAA_NAEBUSDeviceInfo@Graphics@@@Z
	HookMgr::Install(destroyRender, DestroyRenderSetups_hook, DestroyRenderSetups_orig); // ?DestroyRenderSetups@CGraphicsEngine@NGraphicsEngine@@AEAAXXZ
}