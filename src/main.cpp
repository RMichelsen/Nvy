#include "common/scoped_exit.hpp"
#include "nvim/nvim.h"
#include "renderer/renderer.h"
#include <filesystem>
#include <format>
#include <optional>

struct Context
{
    bool start_maximized;
    bool start_fullscreen;
    bool disable_fullscreen;
    HWND hwnd;
    Nvim *nvim;
    Renderer *renderer;
    bool dead_char_pending;
    bool xbuttons[2];
    float buffered_scroll_amount;
    GridPoint cached_cursor_grid_pos;
    WINDOWPLACEMENT saved_window_placement;
    UINT saved_dpi_scaling;
    uint32_t saved_window_width;
    uint32_t saved_window_height;
    HKL hkl;
    HIMC himc{ 0 };
    DWORD imc_conversion{ IME_CMODE_ALPHANUMERIC };
    DWORD imc_sentence{ IME_SMODE_NONE };
};

void ResetImcStatus(Context *context, bool keep_old = true)
{
    if (keep_old)
    {
        ImmGetConversionStatus(context->himc, &context->imc_conversion, &context->imc_sentence);
    }
    ImmSetConversionStatus(context->himc, IME_CMODE_ALPHANUMERIC, IME_SMODE_NONE);
}

void RestoreImcStatus(Context *context)
{
    ImmSetConversionStatus(context->himc, context->imc_conversion, context->imc_sentence);
}

void ToggleFullscreen(HWND hwnd, Context *context)
{
    DWORD style = GetWindowLong(hwnd, GWL_STYLE);
    MONITORINFO mi{ .cbSize = sizeof(MONITORINFO) };
    if (style & WS_OVERLAPPEDWINDOW)
    {
        if (GetWindowPlacement(hwnd, &context->saved_window_placement) && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }
    else
    {
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &context->saved_window_placement);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

void ProcessMPackMessage(Context *context, mpack_tree_t *tree)
{
    MPackMessageResult result = MPackExtractMessageResult(tree);

    switch (result.type)
    {
    case MPackMessageType::Response: {
        assert(result.response.msg_id <= context->nvim->next_msg_id);
        switch (context->nvim->msg_id_to_method[result.response.msg_id])
        {
        case NvimRequest::nvim_eval: {
            Vec<char> guifont_buffer;
            auto txt = mpack_node_str(result.params);
            NvimParseConfig(context->nvim, result.params, &guifont_buffer);
            if (!guifont_buffer.empty())
            {
                bool updated = RendererUpdateGuiFont(context->renderer, guifont_buffer.data(), strlen(guifont_buffer.data()));
                if (!updated)
                {
                    guifont_buffer[guifont_buffer.size() - 1] = '"';
                    guifont_buffer.push_back('\0');
                    const char *error = "echom \"Unknown font: ";
                    char *command = static_cast<char *>(malloc(strlen(error) + guifont_buffer.size()));
                    memcpy(command, error, strlen(error));
                    memcpy(command + strlen(error), guifont_buffer.data(), guifont_buffer.size());
                    NvimSendCommand(context->nvim, command);
                    free(command);
                }
            }
            ResetImcStatus(context, false);
        }
        break;
        case NvimRequest::vim_get_api_info:
        case NvimRequest::nvim_input:
        case NvimRequest::nvim_input_mouse:
        case NvimRequest::nvim_command: {
        }
        break;
        }
    }
    break;
    case MPackMessageType::Notification: {
        if (MPackMatchString(result.notification.name, "redraw"))
        {
            RendererRedraw(context->renderer, result.params, context->start_maximized);
        }
        else if (MPackMatchString(result.notification.name, "mode_changed") && context->himc)
        {
            if (mpack_node_type(result.params) == mpack_type_array && mpack_node_array_length(result.params) > 0)
            {
                auto param1 = mpack_node_array_at(result.params, 0);
                std::string mode(mpack_node_str(param1), mpack_node_strlen(param1));
                if (mode == "n")
                {
                    ResetImcStatus(context);
                }
                else if (mode == "i")
                {
                    RestoreImcStatus(context);
                }
            }
        }
    }
    break;
    case MPackMessageType::Request: {
        // const char *str = mpack_node_str(result.request.method);
        if (MPackMatchString(result.request.method, "vimenter"))
        {
            // nvim has read user init file, we can now request info if we want
            // like additional startup settings or something else
            NvimSendResponse(context->nvim, result.request.msg_id);
            NvimQueryConfig(context->nvim);
        }
    }
    break;
    }
}

void SendResizeIfNecessary(Context *context, int rows, int cols)
{
    if (!context->renderer->grid_initialized) return;

    if (rows != context->renderer->grid_rows || cols != context->renderer->grid_cols)
    {
        NvimSendResize(context->nvim, rows, cols);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Context *context = reinterpret_cast<Context *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE)
    {
        LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return 0;
    }

    switch (msg)
    {
    case WM_SIZE: {
        if (wparam != SIZE_MINIMIZED)
        {
            uint32_t new_width = LOWORD(lparam);
            uint32_t new_height = HIWORD(lparam);
            context->saved_window_height = new_height;
            context->saved_window_width = new_width;
        }
    }
        return 0;
    case WM_DPICHANGED: {
        UINT current_dpi = HIWORD(wparam);
        RECT *const prcNewWindow = (RECT *)lparam;

        context->renderer->dpi_scale = current_dpi / 96.0f;
        RendererUpdateFont(context->renderer, context->renderer->last_requested_font_size);
        auto [rows, cols] = RendererPixelsToGridSize(context->renderer, prcNewWindow->right - prcNewWindow->left, prcNewWindow->bottom - prcNewWindow->top);
        SendResizeIfNecessary(context, rows, cols);
        context->saved_dpi_scaling = current_dpi;

        SetWindowPos(
            hwnd, NULL, 0, 0, prcNewWindow->right - prcNewWindow->left, prcNewWindow->bottom - prcNewWindow->top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
        return 0;
    case WM_DESTROY: {
        PostQuitMessage(0);
    }
        return 0;
    case WM_NVIM_MESSAGE: {
        mpack_tree_t *tree = reinterpret_cast<mpack_tree_t *>(wparam);
        ProcessMPackMessage(context, tree);
    }
        return 0;
    case WM_RENDERER_FONT_UPDATE: {
        auto [rows, cols] = RendererPixelsToGridSize(context->renderer, context->renderer->pixel_size.width, context->renderer->pixel_size.height);
        SendResizeIfNecessary(context, rows, cols);
    }
        return 0;
    case WM_INPUTLANGCHANGE: {
        HKL hkl = (HKL)lparam;
        context->hkl = hkl;
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR: {
        context->dead_char_pending = true;
    }
        return 0;
    case WM_CHAR: {
        context->dead_char_pending = false;
        // Special case for <LT>
        if (wparam == 0x3C)
        {
            NvimSendInput(context->nvim, "<LT>");
            return 0;
        }
        if (wparam == 0x00)
        {
            NvimSendInput(context->nvim, "<Nul>");
            return 0;
        }
        NvimSendChar(context->nvim, static_cast<wchar_t>(wparam));
    }
        return 0;
    case WM_SYSCHAR: {
        if (static_cast<int>(wparam) == VK_SPACE)
        {
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }
        else
        {
            context->dead_char_pending = false;
            NvimSendSysChar(context->nvim, static_cast<wchar_t>(wparam));
        }
    }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        // Special case for <ALT+ENTER> (fullscreen transition)
        if (!context->disable_fullscreen && ((GetKeyState(VK_LMENU) & 0x80) != 0) && wparam == VK_RETURN)
        {
            ToggleFullscreen(hwnd, context);
        }
        else if (((GetKeyState(VK_LMENU) & 0x80) != 0) && wparam == VK_F4)
        {
            NvimQuit(context->nvim);
        }
        else
        {
            LONG msg_pos = GetMessagePos();
            POINTS pt = MAKEPOINTS(msg_pos);
            MSG current_msg{
                .hwnd = hwnd, .message = msg, .wParam = wparam, .lParam = lparam, .time = static_cast<DWORD>(GetMessageTime()), .pt = POINT{ pt.x, pt.y }
            };

            if (context->dead_char_pending)
            {
                if (static_cast<int>(wparam) == VK_SPACE || static_cast<int>(wparam) == VK_BACK || static_cast<int>(wparam) == VK_ESCAPE)
                {
                    context->dead_char_pending = false;
                    TranslateMessage(&current_msg);
                    return 0;
                }
            }

            bool altgr_down = (GetKeyState(VK_RMENU) & 0x80) != 0;
            bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x80) != 0;
            wchar_t wchar = static_cast<wchar_t>(MapVirtualKeyEx(wparam, MAPVK_VK_TO_CHAR, context->hkl));
            if (!altgr_down && ctrl_down && wchar)
            {
                NvimSendSysChar(context->nvim, wchar);
                return 0;
            }

            // If none of the special keys were hit, process in WM_CHAR
            if (!NvimProcessKeyDown(context->nvim, static_cast<int>(wparam)))
            {
                TranslateMessage(&current_msg);
            }
        }
    }
        return 0;
    case WM_MOUSEMOVE: {
        POINTS cursor_pos = MAKEPOINTS(lparam);
        GridPoint grid_pos = RendererCursorToGridPoint(context->renderer, cursor_pos.x, cursor_pos.y);
        if (context->cached_cursor_grid_pos.col != grid_pos.col || context->cached_cursor_grid_pos.row != grid_pos.row)
        {
            switch (wparam)
            {
            case MK_LBUTTON: {
                NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Drag, grid_pos.row, grid_pos.col);
            }
            break;
            case MK_MBUTTON: {
                NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Drag, grid_pos.row, grid_pos.col);
            }
            break;
            case MK_RBUTTON: {
                NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Drag, grid_pos.row, grid_pos.col);
            }
            break;
            }
            context->cached_cursor_grid_pos = grid_pos;
        }
    }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        POINTS cursor_pos = MAKEPOINTS(lparam);
        auto [row, col] = RendererCursorToGridPoint(context->renderer, cursor_pos.x, cursor_pos.y);
        if (msg == WM_LBUTTONDOWN)
        {
            NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Press, row, col);
        }
        else if (msg == WM_MBUTTONDOWN)
        {
            NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Press, row, col);
        }
        else if (msg == WM_RBUTTONDOWN)
        {
            NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Press, row, col);
        }
        else if (msg == WM_LBUTTONUP)
        {
            NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Release, row, col);
        }
        else if (msg == WM_MBUTTONUP)
        {
            NvimSendMouseInput(context->nvim, MouseButton::Middle, MouseAction::Release, row, col);
        }
        else if (msg == WM_RBUTTONUP)
        {
            NvimSendMouseInput(context->nvim, MouseButton::Right, MouseAction::Release, row, col);
        }
    }
        return 0;
    case WM_XBUTTONDOWN: {
        int button = GET_XBUTTON_WPARAM(wparam);
        if (button == XBUTTON1 && !context->xbuttons[0])
        {
            NvimSendInput(context->nvim, "<C-o>");
            context->xbuttons[0] = true;
        }
        else if (button == XBUTTON2 && !context->xbuttons[1])
        {
            NvimSendInput(context->nvim, "<C-i>");
            context->xbuttons[1] = true;
        }
    }
        return 0;
    case WM_XBUTTONUP: {
        int button = GET_XBUTTON_WPARAM(wparam);
        if (button == XBUTTON1)
        {
            context->xbuttons[0] = false;
        }
        else if (button == XBUTTON2)
        {
            context->xbuttons[1] = false;
        }
    }
        return 0;
    case WM_MOUSEWHEEL: {
        bool should_resize_font = (GetKeyState(VK_CONTROL) & 0x80) != 0;

        POINTS screen_point = MAKEPOINTS(lparam);
        POINT client_point{
            .x = static_cast<LONG>(screen_point.x),
            .y = static_cast<LONG>(screen_point.y),
        };
        ScreenToClient(hwnd, &client_point);

        float scroll_amount = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
        context->buffered_scroll_amount += scroll_amount;

        auto [row, col] = RendererCursorToGridPoint(context->renderer, client_point.x, client_point.y);

        MouseAction action;
        if (context->buffered_scroll_amount > 0.0)
        {
            scroll_amount = 1.0f;
            action = MouseAction::MouseWheelUp;
        }
        else
        {
            scroll_amount = -1.0f;
            action = MouseAction::MouseWheelDown;
        }

        while (abs(context->buffered_scroll_amount) >= 1.0f)
        {
            if (should_resize_font)
            {
                RendererUpdateFont(context->renderer, context->renderer->last_requested_font_size + (scroll_amount * 2.0f));
                auto [rows, cols] = RendererPixelsToGridSize(context->renderer, context->renderer->pixel_size.width, context->renderer->pixel_size.height);
                SendResizeIfNecessary(context, rows, cols);
            }
            else
            {
                for (int i = 0; i < abs(scroll_amount); ++i)
                {
                    NvimSendMouseInput(context->nvim, MouseButton::Wheel, action, row, col);
                }
            }

            context->buffered_scroll_amount -= scroll_amount;
        }
    }
        return 0;
    case WM_DROPFILES: {
        wchar_t file_to_open[MAX_PATH];
        uint32_t num_files = DragQueryFileW(reinterpret_cast<HDROP>(wparam), 0xFFFFFFFF, file_to_open, MAX_PATH);
        for (int i = 0; i < num_files; ++i)
        {
            DragQueryFileW(reinterpret_cast<HDROP>(wparam), i, file_to_open, MAX_PATH);

            // Click left mouse button to ensure file is opened in the appropriate neovim split
            POINT screen_point;
            GetCursorPos(&screen_point);
            POINT client_point{
                .x = static_cast<LONG>(screen_point.x),
                .y = static_cast<LONG>(screen_point.y),
            };
            ScreenToClient(hwnd, &client_point);
            auto [row, col] = RendererCursorToGridPoint(context->renderer, client_point.x, client_point.y);
            NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Press, row, col);
            NvimSendMouseInput(context->nvim, MouseButton::Left, MouseAction::Release, row, col);

            // Not the most elegant solution, but must wait for mouseclick to be registered with nvim
            Sleep(10);

            NvimOpenFile(context->nvim, file_to_open, (GetKeyState(VK_CONTROL) & 0x80) != 0);
        }
    }
        return 0;
    case WM_SETFOCUS: {
        NvimSetFocus(context->nvim);
    }
        return 0;
    case WM_KILLFOCUS: {
        NvimKillFocus(context->nvim);
    }
        return 0;
    case WM_CLOSE: {
        NvimQuit(context->nvim);
    }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

BOOL ShouldUseDarkMode()
{
    constexpr const LPCWSTR key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    constexpr const LPCWSTR value = L"AppsUseLightTheme";

    DWORD type;
    DWORD data;
    DWORD size = sizeof(DWORD);
    LSTATUS st = RegGetValue(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, &type, &data, &size);

    if (st == ERROR_SUCCESS && type == REG_DWORD) return data == 0;
    return false;
}

void AutoSetCppEnv()
{
    auto exec = [](const WCHAR *cmd) -> std::optional<std::string> {
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        HANDLE g_hChildStd_IN_Rd = NULL;
        HANDLE g_hChildStd_IN_Wr = NULL;
        HANDLE g_hChildStd_OUT_Rd = NULL;
        HANDLE g_hChildStd_OUT_Wr = NULL;

        CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0);
        SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);
        CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0);
        SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0);

        PROCESS_INFORMATION piProcInfo;
        STARTUPINFO siStartInfo;
        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
        ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
        siStartInfo.cb = sizeof(STARTUPINFO);
        siStartInfo.hStdError = g_hChildStd_OUT_Wr;
        siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
        siStartInfo.hStdInput = g_hChildStd_IN_Rd;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        auto bSuccess = CreateProcess(NULL, (WCHAR *)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo);
        if (bSuccess)
        {
            CloseHandle(piProcInfo.hProcess);
            CloseHandle(piProcInfo.hThread);
            CloseHandle(g_hChildStd_OUT_Wr);
            CloseHandle(g_hChildStd_IN_Rd);
        };

        DWORD dwRead = 0;
        CHAR chBuf[4096];
        std::string output;
        for (;;)
        {
            bSuccess = ReadFile(g_hChildStd_OUT_Rd, chBuf, 4096, &dwRead, NULL);
            if (!bSuccess || dwRead == 0) break;
            output += std::string(chBuf, dwRead);
        };

        auto it = std::remove_if(output.begin(), output.end(), [](auto c) {
            return c == '\r' || c == '\n';
        });
        output.erase(it, output.end());
        return output;
    };

    auto vswhere = L"C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe";
    if (!std::filesystem::exists(vswhere)) return;

    auto output = exec(std::format(L"\"{}\" -latest -property installationPath", vswhere).c_str());
    if (!output) return;

    std::string vspath(*output);
    vspath += "\\VC\\Tools\\MSVC\\";
    std::filesystem::directory_iterator it(vspath);
    while (!it->is_directory())
    {
        ++it;
    }
    if (!it->exists()) return;

    vspath = std::format("{}/bin/Hostx64/x64", it->path().generic_string());
    if (!std::filesystem::exists(vspath + "/cl.exe")) return;

    std::string envpath = std::getenv("path");
    envpath += ";" + vspath;
    _putenv_s("path", envpath.c_str());
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev_instance, _In_ LPWSTR p_cmd_line, _In_ int n_cmd_show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    AutoSetCppEnv();

    int n_args;
    LPWSTR *cmd_line_args = CommandLineToArgvW(GetCommandLineW(), &n_args);
    bool start_maximized = false;
    bool start_fullscreen = false;
    bool disable_ligatures = false;
    bool disable_fullscreen = false;
    float linespace_factor = 1.0f;
    int64_t start_rows = 0;
    int64_t start_cols = 0;
    int64_t start_pos_x = CW_USEDEFAULT;
    int64_t start_pos_y = CW_USEDEFAULT;

    struct WArg
    {
        wchar_t *arg;
        size_t len;
    };
    Vec<WArg> nvim_args;

    // Skip argv[0]
    for (int i = 1; i < n_args; ++i)
    {
        if (!wcscmp(cmd_line_args[i], L"--maximize"))
        {
            start_maximized = true;
        }
        else if (!wcscmp(cmd_line_args[i], L"--fullscreen"))
        {
            start_fullscreen = true;
        }
        else if (!wcscmp(cmd_line_args[i], L"--disable-ligatures"))
        {
            disable_ligatures = true;
        }
        else if (!wcscmp(cmd_line_args[i], L"--disable-fullscreen"))
        {
            disable_fullscreen = true;
        }
        else if (!wcsncmp(cmd_line_args[i], L"--geometry=", wcslen(L"--geometry=")))
        {
            wchar_t *end_ptr;
            start_cols = wcstol(&cmd_line_args[i][11], &end_ptr, 10);
            start_rows = wcstol(end_ptr + 1, nullptr, 10);
        }
        else if (!wcsncmp(cmd_line_args[i], L"--position=", wcslen(L"--position=")))
        {
            wchar_t *end_ptr;
            start_pos_x = wcstol(&cmd_line_args[i][11], &end_ptr, 10);
            start_pos_y = wcstol(end_ptr + 1, nullptr, 10);
        }
        else if (!wcsncmp(cmd_line_args[i], L"--linespace-factor=", wcslen(L"--linespace-factor=")))
        {
            wchar_t *end_ptr;
            float factor = wcstof(&cmd_line_args[i][19], &end_ptr);
            if (factor > 0.0f && factor < 20.0f)
            {
                linespace_factor = factor;
            }
        }
        else if (!wcsncmp(cmd_line_args[i], L"--run", wcslen(L"--run")))
        {
            auto src = cmd_line_args[i];
            auto len = wcslen(src);
            char text[256]{ 0 };
            std::copy(src + 6, src + len, text);
            auto ret = system(text);
            ret = 0;
        }
        // Otherwise assume the argument is a filename to open
        else
        {
            nvim_args.push_back(WArg{ .arg = cmd_line_args[i], .len = wcslen(cmd_line_args[i]) });
        }
    }

    const wchar_t *window_class_name = L"Nvy_Class";
    const wchar_t *window_title = L"Nvy";
    WNDCLASSEX window_class{ .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = instance,
        .hIcon = static_cast<HICON>(LoadImage(GetModuleHandle(NULL), L"NVIM_ICON", IMAGE_ICON, LR_DEFAULTSIZE, LR_DEFAULTSIZE, 0)),
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .lpszClassName = window_class_name,
        .hIconSm = static_cast<HICON>(LoadImage(GetModuleHandle(NULL), L"NVIM_ICON", IMAGE_ICON, LR_DEFAULTSIZE, LR_DEFAULTSIZE, 0)) };

    if (!RegisterClassEx(&window_class))
    {
        return 1;
    }

    Nvim nvim{};
    Renderer renderer{};
    Context context{ .start_maximized = start_maximized,
        .start_fullscreen = start_fullscreen,
        .disable_fullscreen = disable_fullscreen,
        .nvim = &nvim,
        .renderer = &renderer,
        .saved_window_placement = WINDOWPLACEMENT{ .length = sizeof(WINDOWPLACEMENT) } };

    HWND hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, window_class_name, window_title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, nullptr, nullptr, instance, &context);
    if (hwnd == NULL) return 1;
    context.hwnd = hwnd;
    context.hkl = GetKeyboardLayout(0);
    context.himc = ImmGetContext(hwnd);
    RECT window_rect;
    DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &window_rect, sizeof(RECT));
    HMONITOR monitor = MonitorFromPoint({ window_rect.left, window_rect.top }, MONITOR_DEFAULTTONEAREST);
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &(context.saved_dpi_scaling), &(context.saved_dpi_scaling));
    constexpr int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    BOOL should_use_dark_mode = ShouldUseDarkMode();
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &should_use_dark_mode, sizeof(BOOL));
    RendererInitialize(&renderer, hwnd, disable_ligatures, linespace_factor, context.saved_dpi_scaling);

    // Prepare nvim command line
    size_t nvim_command_len = wcslen(L"nvim --embed");
    for (const auto &nvim_command_arg : nvim_args)
    {
        nvim_command_len += wcslen(L" \"\"") + nvim_command_arg.len;
    }
    wchar_t *nvim_command_line = static_cast<wchar_t *>(malloc(sizeof(wchar_t) * (nvim_command_len + 1)));
    int cur_len = _snwprintf_s(nvim_command_line, nvim_command_len + 1, nvim_command_len, L"nvim --embed");
    for (const auto &[arg, len] : nvim_args)
    {
        cur_len +=
            _snwprintf_s(nvim_command_line + cur_len, nvim_command_len + 1 - cur_len, nvim_command_len - cur_len, L" \"%s\"", arg, static_cast<uint32_t>(len));
    }

    auto scope = scoped_exit([&] {
        RendererShutdown(&renderer);
        NvimShutdown(&nvim);
        UnregisterClass(window_class_name, instance);
        DestroyWindow(hwnd);
        free(nvim_command_line);
    });

    auto nvim_process_ok = NvimInitialize(&nvim, nvim_command_line, hwnd);
    if (!nvim_process_ok)
    {
        MessageBox(hwnd, L"Cannot run nvim process", L"Error", 0);
        return EXIT_FAILURE;
    }

    // Attach the renderer now that the window size is determined
    RendererAttach(context.renderer);
    auto [rows, cols] = RendererPixelsToGridSize(context.renderer, context.renderer->pixel_size.width, context.renderer->pixel_size.height);
    NvimSendUIAttach(context.nvim, rows, cols);

    // Forceably update the window to prevent any frames where the window is blank. Windows API docs
    // specify that SetWindowPos should be called with these arguments after SetWindowLong is called.
    int window_w = 0, window_h = 0;
    if (start_rows != 0 && start_cols != 0)
    {
        PixelSize start_size = RendererGridToPixelSize(context.renderer, start_rows, start_cols);
        window_w = start_size.width;
        window_h = start_size.height;
    }
    else
    {
        window_w = context.renderer->pixel_size.width;
        window_h = context.renderer->pixel_size.height;
    }

    start_pos_x = start_pos_x != CW_USEDEFAULT ? start_pos_x : (GetSystemMetrics(SM_CXSCREEN) - window_w) / 2;
    start_pos_y = start_pos_y != CW_USEDEFAULT ? start_pos_y : (GetSystemMetrics(SM_CYSCREEN) - window_h) / 2;

    if (start_fullscreen)
    {
        ToggleFullscreen(context.hwnd, &context);
    }
    else
    {
        SetWindowPos(hwnd, HWND_TOP, start_pos_x, start_pos_y, window_w, window_h, SWP_SHOWWINDOW);
    }

    MSG msg;
    uint32_t previous_width = 0, previous_height = 0;
    while (GetMessage(&msg, 0, 0, 0))
    {
        //TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (renderer.draw_active) continue;

        if (previous_width != context.saved_window_width || previous_height != context.saved_window_height)
        {
            previous_width = context.saved_window_width;
            previous_height = context.saved_window_height;
            auto [rows, cols] = RendererPixelsToGridSize(context.renderer, context.saved_window_width, context.saved_window_height);
            RendererResize(context.renderer, context.saved_window_width, context.saved_window_height);
            SendResizeIfNecessary(&context, rows, cols);
        }
    }

    return nvim.exit_code;
}
