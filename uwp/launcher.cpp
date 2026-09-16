#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <libuwp.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.UI.Core.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include "disc_identity.h"

int mmx5_runtime_main(int, char **);
namespace {
using namespace winrt;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::AccessCache;
using namespace Windows::UI::Core;
struct FileEntry {
    std::string name, path;
    bool folder;
};
struct LauncherState {
    StorageFolder folder{nullptr};
    std::vector<FileEntry> entries;
    std::string current_folder_path;
    std::string error_message;
    std::string selected_disc_path;
    std::string save_data_path;
    bool external_data = false;
    bool busy = false;
    bool launch = false;
    bool focus_internal = false;
    bool focus_play = false;
    bool external_direct_failed = false;
};
struct GraphicsSettings {
    int supersampling = 1;
    bool antialiasing = true;
    bool widescreen = false;
};
enum class BrowseAction { Internal, External, Child, Parent, Saved, PickFolder };
std::string g_local_path;
std::string g_runtime_data_path;
constexpr wchar_t kDiscAccessToken[] = L"game-disc";
constexpr wchar_t kDataFolderAccessToken[] = L"game-data-folder";
constexpr wchar_t kFolderAccessToken[] = L"game-folder";

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string(std::istreambuf_iterator<char>(in), {}) : std::string{};
}
void save_graphics_settings(const GraphicsSettings &settings, const std::string &root) {
    const auto root_path = std::filesystem::u8path(root);
    std::filesystem::create_directories(root_path);
    {
        std::ofstream out(root_path / "settings.toml", std::ios::trunc);
        out << "[video]\n"
            << "renderer          = \"opengl\"\n"
            << "supersampling     = " << settings.supersampling << "\n"
            << "antialiasing      = " << (settings.antialiasing ? "true" : "false") << "\n"
            << "texture_filtering = \"nearest\"\n"
            << "aspect_ratio      = \"" << (settings.widescreen ? "16:9" : "4:3") << "\"\n"
            << "fullscreen        = 1\n"
            << "low_latency_input = true\n"
            << "vsync             = \"on\"\n";
    }
    std::filesystem::create_directories(root_path / "mods");
    std::ofstream mods(root_path / "mods/state.toml", std::ios::trunc);
    mods << "format_version = 2\n\n"
         << "[[package]]\nid = \"mmx5.enhancement.widescreen\"\nversion = \"1.0.0\"\n\n"
         << "[[package]]\nid = \"mmx5.enhancement.frame-interpolation\"\nversion = \"1.0.0\"\n\n"
         << "[[feature]]\npackage_id = \"mmx5.enhancement.widescreen\"\nid = \"widescreen\"\n"
         << "enabled = " << (settings.widescreen ? "true" : "false") << "\n\n"
         << "[[feature]]\npackage_id = \"mmx5.enhancement.frame-interpolation\"\nid = \"frame-interpolation\"\n"
         << "enabled = false\n"
         << "[feature.values]\nrate = \"display\"\n";
}

void enforce_supported_graphics_settings(const std::string &root) {
    const auto base = std::filesystem::u8path(root);
    const auto settings_path = base / "settings.toml";
    auto settings = read_text(settings_path);
    bool changed = false;
    const std::string bilinear = "texture_filtering = \"bilinear\"";
    for (size_t at = 0; (at = settings.find(bilinear, at)) != std::string::npos;) {
        settings.replace(at, bilinear.size(), "texture_filtering = \"nearest\"");
        at += 29;
        changed = true;
    }
    if (changed) {
        std::ofstream out(settings_path, std::ios::trunc);
        out << settings;
    }

    const auto state_path = base / "mods/state.toml";
    auto state = read_text(state_path);
    changed = false;
    size_t section = 0;
    while ((section = state.find("[[feature]]", section)) != std::string::npos) {
        const size_t end = state.find("[[feature]]", section + 11);
        const size_t package = state.find("package_id = \"mmx5.enhancement.frame-interpolation\"", section);
        if (package != std::string::npos && (end == std::string::npos || package < end)) {
            const size_t enabled = state.find("enabled = true", package);
            if (enabled != std::string::npos && (end == std::string::npos || enabled < end)) {
                state.replace(enabled, 14, "enabled = false");
                changed = true;
            }
        }
        if (end == std::string::npos)
            break;
        section = end;
    }
    if (changed) {
        std::ofstream out(state_path, std::ios::trunc);
        out << state;
    }
}

bool path_is_within(const std::string &child, const std::string &parent) {
    auto child_path = std::filesystem::u8path(child).lexically_normal().wstring();
    auto parent_path = std::filesystem::u8path(parent).lexically_normal().wstring();
    std::transform(child_path.begin(), child_path.end(), child_path.begin(), ::towlower);
    std::transform(parent_path.begin(), parent_path.end(), parent_path.begin(), ::towlower);
    if (!parent_path.empty() && parent_path.back() != L'\\')
        parent_path.push_back(L'\\');
    return child_path.size() >= parent_path.size() && child_path.compare(0, parent_path.size(), parent_path) == 0;
}

Windows::Foundation::IAsyncAction select_data_root(std::shared_ptr<LauncherState> browser, StorageFile file) {
    const auto file_path = to_string(file.Path());
    if (path_is_within(file_path, g_local_path)) {
        browser->save_data_path = g_local_path;
        browser->external_data = false;
        co_return;
    }
    const auto parent = co_await file.GetParentAsync();
    if (!parent)
        throw hresult_error(E_FAIL, L"The game folder is unavailable.");
    const auto data_folder =
        co_await parent.CreateFolderAsync(L"MegaManX5Recomp", CreationCollisionOption::OpenIfExists);
    StorageApplicationPermissions::FutureAccessList().AddOrReplace(kDataFolderAccessToken, data_folder);
    browser->save_data_path = to_string(data_folder.Path());
    browser->external_data = true;
}

Windows::Foundation::IAsyncAction list_folder(std::shared_ptr<LauncherState> browser, StorageFolder folder) {
    const auto items = co_await folder.GetItemsAsync();
    std::vector<FileEntry> entries;
    for (const auto &item : items) {
        const bool is_folder = item.IsOfType(StorageItemTypes::Folder);
        auto name = to_string(item.Name());
        std::wstring ext = std::filesystem::path(item.Name().c_str()).extension().wstring();
        if (is_folder || _wcsicmp(ext.c_str(), L".cue") == 0 || _wcsicmp(ext.c_str(), L".bin") == 0)
            entries.push_back({name, to_string(item.Path()), is_folder});
    }
    std::sort(entries.begin(), entries.end(), [](const FileEntry &lhs, const FileEntry &rhs) {
        if (lhs.folder != rhs.folder)
            return lhs.folder > rhs.folder;
        return lhs.name < rhs.name;
    });
    browser->folder = folder;
    browser->current_folder_path = to_string(folder.Path());
    browser->entries = std::move(entries);
}
fire_and_forget browse(std::shared_ptr<LauncherState> browser, BrowseAction action, std::string path = {}) {
    browser->busy = true;
    browser->error_message.clear();
    try {
        StorageFolder folder{nullptr};
        if (action == BrowseAction::Internal) {
            folder = co_await ApplicationData::Current().LocalFolder().CreateFolderAsync(
                L"Game", CreationCollisionOption::OpenIfExists);
        } else if (action == BrowseAction::External) {
            auto drives = KnownFolders::RemovableDevices();
            const auto roots = co_await drives.GetFoldersAsync();
            for (const auto &root : roots) {
                if (_wcsicmp(root.Path().c_str(), L"E:\\") == 0) {
                    folder = root;
                    break;
                }
            }
            if (!folder && roots.Size() == 1)
                folder = roots.GetAt(0);
            if (!folder && roots.Size() > 1)
                folder = drives;
            if (!folder) {
                browser->external_direct_failed = true;
                browser->error_message =
                    "No external drive found. Connect your USB drive, then try External Storage again.";
            } else {
                browser->external_direct_failed = false;
            }
        } else if (action == BrowseAction::PickFolder) {
            FolderPicker picker;
            picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
            picker.ViewMode(PickerViewMode::List);
            picker.FileTypeFilter().Append(L"*");
            folder = co_await picker.PickSingleFolderAsync();
            if (folder) {
                StorageApplicationPermissions::FutureAccessList().AddOrReplace(kFolderAccessToken, folder);
                browser->external_direct_failed = false;
            }
        } else if (action == BrowseAction::Child) {
            folder = co_await browser->folder.GetFolderAsync(to_hstring(path));
        } else if (action == BrowseAction::Parent) {
            folder = co_await browser->folder.GetParentAsync();
        } else {
            auto access = StorageApplicationPermissions::FutureAccessList();
            if (access.ContainsItem(kFolderAccessToken))
                folder = co_await access.GetFolderAsync(kFolderAccessToken);
        }
        if (folder)
            co_await list_folder(browser, folder);
    } catch (const hresult_error &e) {
        if (action == BrowseAction::External)
            browser->external_direct_failed = true;
        browser->error_message = to_string(e.message()) + " Reconnect the drive or choose the folder manually.";
    } catch (const std::exception &e) {
        browser->error_message = e.what();
    }
    browser->busy = false;
}
fire_and_forget select_disc(std::shared_ptr<LauncherState> browser, std::string path, bool launch_when_ready = false) {
    browser->busy = true;
    browser->error_message.clear();
    apartment_context ui;
    std::string verification_error;
    StorageFile selected_file{nullptr};
    try {
        selected_file = co_await StorageFile::GetFileFromPathAsync(to_hstring(path));
        co_await resume_background();
        const auto disc_info = PSXRecompV4::identify_disc(std::filesystem::u8path(path), "SLUS-01334", 0, false, false);
        if (!disc_info.serial_matches || !disc_info.toc_opened)
            verification_error = "Select Mega Man X5 USA (SLUS-01334). " + disc_info.detail;
    } catch (const hresult_error &e) {
        verification_error = to_string(e.message());
    } catch (const std::exception &e) {
        verification_error = e.what();
    }
    co_await ui;
    if (verification_error.empty()) {
        browser->selected_disc_path = std::move(path);
        if (!launch_when_ready)
            browser->focus_play = true;
        if (selected_file) {
            try {
                StorageApplicationPermissions::FutureAccessList().AddOrReplace(kDiscAccessToken, selected_file);
                co_await select_data_root(browser, selected_file);
                std::ofstream saved(std::filesystem::u8path(g_local_path) / "selected-disc.txt", std::ios::trunc);
                saved << browser->selected_disc_path;
            } catch (const hresult_error &e) {
                browser->save_data_path = g_local_path;
                browser->external_data = false;
                browser->error_message =
                    "The game is selected, but USB app-data could not be created; saves will use internal storage. " +
                    to_string(e.message());
            }
            if (launch_when_ready)
                browser->launch = true;
        }
    } else
        browser->error_message = std::move(verification_error);
    browser->busy = false;
}
fire_and_forget restore_disc(std::shared_ptr<LauncherState> browser) {
    browser->busy = true;
    try {
        StorageFile file{nullptr};
        auto access = StorageApplicationPermissions::FutureAccessList();
        if (access.ContainsItem(kDiscAccessToken))
            file = co_await access.GetFileAsync(kDiscAccessToken);
        if (!file) {
            const auto saved = read_text(std::filesystem::u8path(g_local_path) / "selected-disc.txt");
            if (!saved.empty())
                file = co_await StorageFile::GetFileFromPathAsync(to_hstring(saved));
        }
        if (file) {
            const auto parent = co_await file.GetParentAsync();
            if (parent)
                co_await list_folder(browser, parent);
            const auto path = to_string(file.Path());
            try {
                co_await select_data_root(browser, file);
            } catch (const hresult_error &) {
                browser->save_data_path = g_local_path;
                browser->external_data = false;
            }
            browser->busy = false;
            select_disc(browser, path, true);
            co_return;
        }
        const auto local_game = co_await ApplicationData::Current().LocalFolder().CreateFolderAsync(
            L"Game", CreationCollisionOption::OpenIfExists);
        co_await list_folder(browser, local_game);
        browser->focus_internal = true;
    } catch (const hresult_error &) {
        browser->error_message =
            "The saved game file is unavailable. Reconnect the USB drive or choose the folder again.";
        browser->focus_internal = true;
    } catch (const std::exception &e) {
        browser->error_message = e.what();
    }
    browser->busy = false;
}
void stage_runtime_files(const std::string &root) {
    const auto installed =
        std::filesystem::path(Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str());
    auto root_path = std::filesystem::u8path(root);
    std::filesystem::create_directories(root_path / "bios");
    std::filesystem::create_directories(root_path / "saves");
    std::filesystem::copy_file(installed / "game.toml", root_path / "game.toml",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(installed / "bios/openbios.bin", root_path / "bios/openbios.bin",
                               std::filesystem::copy_options::overwrite_existing);
    if (std::filesystem::exists(installed / "mods")) {
        std::filesystem::copy(installed / "mods", root_path / "mods",
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::current_path(root_path);
}
int run_runtime(const std::string &disc_path, const std::string &save_data_path) {
    g_runtime_data_path = save_data_path.empty() ? g_local_path : save_data_path;
    stage_runtime_files(g_runtime_data_path);
    enforce_supported_graphics_settings(g_runtime_data_path);
    std::vector<std::string> args = {g_local_path + "/MegaManX5.exe",
                                     "--game",
                                     g_runtime_data_path + "/game.toml",
                                     "--bios",
                                     g_runtime_data_path + "/bios/openbios.bin",
                                     "--disc",
                                     disc_path,
                                     "--memcard-dir",
                                     g_runtime_data_path + "/saves",
                                     "--no-launcher",
                                     "--renderer",
                                     "opengl"};
    std::vector<char *> argv;
    for (auto &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    return mmx5_runtime_main(static_cast<int>(args.size()), argv.data());
}
}

extern "C" const char *psx_uwp_data_path() {
    return (g_runtime_data_path.empty() ? g_local_path : g_runtime_data_path).c_str();
}

extern "C" void psx_uwp_save_graphics_settings(int supersampling, int antialiasing, int widescreen) {
    GraphicsSettings settings;
    settings.supersampling = std::clamp(supersampling, 1, 4);
    settings.antialiasing = antialiasing != 0;
    settings.widescreen = widescreen != 0;
    save_graphics_settings(settings, g_runtime_data_path.empty() ? g_local_path : g_runtime_data_path);
}

extern "C" int SDL_main(int, char **) {
    g_local_path = winrt::to_string(ApplicationData::Current().LocalFolder().Path());
    FILE *log = nullptr;
    freopen_s(&log, (g_local_path + "/runtime.log").c_str(), "w", stderr);
    auto mark = [](const char *text) {
        std::fprintf(stderr, "launcher: %s\n", text);
        std::fflush(stderr);
    };
    mark("entered SDL_main");
    g_runtime_data_path = g_local_path;
    try {
        stage_runtime_files(g_local_path);
    } catch (const std::exception &e) {
        SDL_Log("Storage initialization failed: %s", e.what());
        return 1;
    }
    mark("staged runtime files");
    _putenv_s("PSX_OVERLAY_AUTOCOMPILE_OFF", "1");
    _putenv_s("PSX_BIOS_HLE", "0");
    int screen_width = 1920;
    int screen_height = 1080;
    const auto device_family = Windows::System::Profile::AnalyticsInfo::VersionInfo().DeviceFamily();
    if (device_family == L"Windows.Xbox") {
        uwp_GetActualSize(&screen_width, &screen_height);
    }
    if (screen_width <= 0 || screen_height <= 0) {
        screen_width = 1920;
        screen_height = 1080;
    }
    uwp_SetScreenSize(screen_width, screen_height);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0)
        return 1;
    mark("initialized SDL");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    auto window = SDL_CreateWindow("Mega Man X5 Recomp UWP", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   screen_width,
                                   screen_height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (!window) {
        SDL_Log("Window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    mark("created window");
    auto gl_context = SDL_GL_CreateContext(window);
    if (!gl_context || SDL_GL_MakeCurrent(window, gl_context) != 0) {
        SDL_Log("OpenGL context: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    mark("created window and renderer");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigNavCursorVisibleAlways = true;
    io.IniFilename = nullptr;
    const float ui_scale = std::clamp(static_cast<float>(screen_height) / 1080.0f, 1.0f, 2.0f);
    io.FontGlobalScale = ui_scale;
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(22.0f, 18.0f);
    style.ItemSpacing = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.055f, 0.10f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.045f, 0.085f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.12f, 0.42f, 0.72f, 0.65f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.08f, 0.25f, 0.43f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.10f, 0.40f, 0.70f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.05f, 0.55f, 0.92f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.08f, 0.28f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.15f, 0.68f, 1.0f, 1.0f);
    style.ScaleAllSizes(ui_scale);
    if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context) || !ImGui_ImplOpenGL3_Init("#version 120")) {
        SDL_Log("ImGui OpenGL initialization failed: %s", SDL_GetError());
        return 1;
    }
    mark("initialized ImGui");
    auto browser = std::make_shared<LauncherState>();
    restore_disc(browser);
    bool quit = false;
    bool first_frame = true;
    while (!quit && !browser->launch) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                quit = true;
        }
        if (auto core = CoreWindow::GetForCurrentThread())
            core.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (first_frame) {
            int window_width = 0, window_height = 0, drawable_width = 0, drawable_height = 0;
            SDL_GetWindowSize(window, &window_width, &window_height);
            SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
            std::fprintf(
                stderr,
                "launcher: actual=%dx%d window=%dx%d drawable=%dx%d imgui=%.0fx%.0f framebuffer=%.2fx%.2f scale=%.2f\n",
                screen_width, screen_height, window_width, window_height, drawable_width, drawable_height,
                io.DisplaySize.x, io.DisplaySize.y, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y,
                ui_scale);
            std::fflush(stderr);
        }
        const ImVec2 safe_pos(io.DisplaySize.x * 0.06f, io.DisplaySize.y * 0.06f);
        const ImVec2 safe_size(io.DisplaySize.x * 0.88f, io.DisplaySize.y * 0.88f);
        ImGui::SetNextWindowPos(safe_pos);
        ImGui::SetNextWindowSize(safe_size);
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        ImGui::Begin("Mega Man X5 Recomp UWP", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(ImVec4(0.20f, 0.70f, 1.0f, 1.0f), "MEGA MAN X5 RECOMP UWP");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.75f, 1.0f), "     UWP Port: ReviveMe");
        ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.86f, 1.0f),
                           "Select your game once and play. Your disc, saves, and settings are remembered.");
        ImGui::Dummy(ImVec2(0.0f, 6.0f * ui_scale));
        ImGui::BeginDisabled(browser->busy);
        if (ImGui::BeginTable("launcher-layout", 2, ImGuiTableFlags_SizingStretchProp,
                              ImGui::GetContentRegionAvail())) {
            ImGui::TableSetupColumn("Storage", ImGuiTableColumnFlags_WidthStretch, 1.45f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextColumn();
            ImGui::BeginChild("storage-card", ImVec2(0, 0), true);
            ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "GAME STORAGE");
            ImGui::TextColored(ImVec4(0.62f, 0.70f, 0.80f, 1.0f), "Mega Man X5 USA (SLUS-01334) - CUE/BIN");
            const float storage_width = ImGui::GetContentRegionAvail().x;
            const float half = (storage_width - style.ItemSpacing.x) * 0.5f;
            if (browser->focus_internal && !browser->busy)
                ImGui::SetKeyboardFocusHere();
            if (ImGui::Button("Internal storage", ImVec2(half, 38.0f * ui_scale)))
                browse(browser, BrowseAction::Internal);
            if (browser->focus_internal && !browser->busy)
                browser->focus_internal = false;
            ImGui::SameLine();
            if (ImGui::Button("External Storage", ImVec2(half, 38.0f * ui_scale)))
                browse(browser, BrowseAction::External);
            if (browser->external_direct_failed &&
                ImGui::Button("Choose folder manually", ImVec2(-1.0f, 34.0f * ui_scale)))
                browse(browser, BrowseAction::PickFolder);
            if (ImGui::Button("Open saved folder", ImVec2(half, 34.0f * ui_scale)))
                browse(browser, BrowseAction::Saved);
            ImGui::SameLine();
            ImGui::BeginDisabled(!browser->folder);
            if (ImGui::Button("Up one folder", ImVec2(half, 34.0f * ui_scale)))
                browse(browser, BrowseAction::Parent);
            ImGui::EndDisabled();
            ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.75f, 1.0f), "%s",
                               browser->current_folder_path.empty() ? "Choose a storage location"
                                                                    : browser->current_folder_path.c_str());
            ImGui::BeginChild("files", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const auto &entry : browser->entries) {
                const auto label = (entry.folder ? "[Folder]  " : "[Disc]  ") + entry.name;
                if (ImGui::Selectable(label.c_str(), entry.path == browser->selected_disc_path)) {
                    if (entry.folder)
                        browse(browser, BrowseAction::Child, entry.name);
                    else
                        select_disc(browser, entry.path);
                    break;
                }
            }
            if (browser->entries.empty())
                ImGui::TextWrapped(
                    "No game files found here. Upload CUE/BIN files to LocalState/Game or open External Storage.");
            ImGui::EndChild();
            ImGui::EndChild();

            ImGui::TableNextColumn();
            ImGui::BeginChild("settings-card", ImVec2(0, 0), true);
            ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "QUICK START");
            ImGui::TextWrapped("Press Menu + View together while playing to open Display Settings.");
            ImGui::Dummy(ImVec2(0.0f, 10.0f * ui_scale));
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "READY TO PLAY");
            if (!browser->selected_disc_path.empty()) {
                ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.55f, 1.0f), "Game verified and remembered");
                const auto disc_name = std::filesystem::u8path(browser->selected_disc_path).filename().string();
                ImGui::TextWrapped("%s", disc_name.c_str());
                ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.75f, 1.0f), "%s",
                                   browser->external_data ? "Saves and settings: USB / MegaManX5Recomp"
                                                          : "Saves and settings: Xbox internal storage");
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.70f, 0.30f, 1.0f), "Select your .BIN game file to continue");
            }
            if (browser->busy)
                ImGui::TextUnformatted("Reading storage...");
            if (!browser->error_message.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.38f, 1.0f), "%s", browser->error_message.c_str());
            const float play_y = ImGui::GetWindowHeight() - 62.0f * ui_scale;
            if (ImGui::GetCursorPosY() < play_y)
                ImGui::SetCursorPosY(play_y);
            ImGui::BeginDisabled(browser->selected_disc_path.empty());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.02f, 0.52f, 0.88f, 1.0f));
            if (browser->focus_play && !browser->busy)
                ImGui::SetKeyboardFocusHere();
            if (ImGui::Button("PLAY MEGA MAN X5", ImVec2(-1.0f, 46.0f * ui_scale))) {
                browser->launch = true;
            }
            if (browser->focus_play && !browser->busy)
                browser->focus_play = false;
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::Render();
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(8.0f / 255.0f, 18.0f / 255.0f, 35.0f / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (first_frame) {
            mark("presented first launcher frame");
            first_frame = false;
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!browser->launch)
        return 0;
    return run_runtime(browser->selected_disc_path, browser->save_data_path);
}
