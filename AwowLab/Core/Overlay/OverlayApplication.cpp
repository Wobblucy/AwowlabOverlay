#include "OverlayApplication.h"
#include "Core/UnifiedSettings.h"
#include "Core/MobWeightSettings.h"
#include "Core/PhaseSettings.h"
#include "Core/PhaseResolver.h"
#include "Core/LocalizationManager.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/SpellContextMenu.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <optional>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#endif

namespace {

#ifdef _WIN32
// Show Windows folder picker dialog
std::filesystem::path showFolderPickerDialog(const wchar_t* title) {
    std::filesystem::path result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needsUninit = SUCCEEDED(hr);

    IFileOpenDialog* pFileOpen = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

    if (SUCCEEDED(hr)) {
        DWORD options;
        pFileOpen->GetOptions(&options);
        pFileOpen->SetOptions(options | FOS_PICKFOLDERS);
        pFileOpen->SetTitle(title);

        hr = pFileOpen->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    result = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }

    if (needsUninit) {
        CoUninitialize();
    }
    return result;
}
#endif

} // anonymous namespace

OverlayApplication::OverlayApplication() {
    window_ = std::make_unique<OverlayWindow>();
    vulkan_ = std::make_unique<OverlayVulkanContext>();
    logManager_ = std::make_unique<LiveLogManager>();
    stats_ = std::make_unique<LiveCombatStats>();
    colorGen_ = std::make_shared<ActorColorGenerator>();
    meterPanel_ = std::make_unique<UIMeterPanel>(1);  // Instance 1
    meterPanel_->setVisible(true);  // Always visible in overlay mode
    // The overlay draws the phase controls in its own top header row,
    // so keep the meter body from drawing a duplicate set.
    meterPanel_->setPhaseControlsExternal(true);
    deathRecapPanel_ = std::make_unique<UIDeathRecapPanel>();
    breakdownPanel_ = std::make_unique<UIActorBreakdownPanel>();
    avoidanceBreakdownPanel_ = std::make_unique<UIAvoidanceBreakdownPanel>();
    mobWeightPanel_ = std::make_unique<UIMobWeightPanel>();
    phaseEditorPanel_ = std::make_unique<UIPhaseEditorPanel>();
    controls_ = std::make_unique<UIOverlayControls>();
    staleness_ = std::make_unique<UIStalenessIndicator>();
}

OverlayApplication::~OverlayApplication() {
    saveSettings();

    if (logManager_) {
        logManager_->stop();
    }

    if (vulkan_) {
        vulkan_->cleanup();
    }

    if (window_) {
        window_->destroy();
    }
}

void OverlayApplication::setLogsFolder(const std::filesystem::path& folder) {
    logsFolder_ = folder;
}

int OverlayApplication::run() {
    loadSettings();

    // Initialize window
    if (!initWindow()) {
        std::cerr << "Failed to create overlay window\n";
        return 1;
    }

    // Initialize log manager
    if (!initLogManager()) {
        std::cerr << "Failed to initialize log manager\n";
        return 1;
    }

    // Register the system-wide lock toggle. Failure is non-fatal - the
    // user can still lock via the button; they just have to avoid
    // locking without a way back out.
    registerGlobalHotkeys();

    // Main loop (30 FPS target)
    constexpr double TARGET_FRAME_TIME = 1.0 / 30.0;
    double lastFrameTime = glfwGetTime();

    while (!window_->shouldClose()) {
        double currentTime = glfwGetTime();
        double elapsed = currentTime - lastFrameTime;
        if (elapsed < TARGET_FRAME_TIME) {
            double sleepTime = TARGET_FRAME_TIME - elapsed;
            if (sleepTime > 0.001) {
                glfwWaitEventsTimeout(sleepTime);
            }
            continue;
        }
        lastFrameTime = currentTime;

        processFrame();
    }

    unregisterGlobalHotkeys();
    return 0;
}

bool OverlayApplication::initWindow() {
    // Default size (50% larger than original 300x400)
    int width = 450;
    int height = 600;

    if (!window_->create(width, height, "AwowLab Overlay")) {
        return false;
    }

    window_->setAlwaysOnTop(true);
    window_->setOpacity(0.85f);

    // Hand the bundled font (if any) to the Vulkan context before it
    // builds the ImGui atlas
    if (uiFontData_ && uiFontSize_ > 0) {
        vulkan_->setUiFont(uiFontData_, uiFontSize_);
    }

    // Initialize Vulkan rendering context
    if (!vulkan_->init(window_->getWindow())) {
        std::cerr << "Failed to initialize Vulkan context\n";
        return false;
    }

    // Scale up UI text/elements by 50%. The bundled font is baked at the
    // scaled size already (crisper than scaling a 13 px bitmap); the
    // built-in fallback font only exists at 13 px, so scale it at runtime.
    ImGui::GetIO().FontGlobalScale = vulkan_->hasCustomFont() ? 1.0f : 1.5f;

    return true;
}

bool OverlayApplication::initLogManager() {
    if (logsFolder_.empty()) {
        // Check if we have a saved logs folder from settings
        const auto& savedFolder = SettingsCache::instance().get().overlayLogsFolder;
        if (!savedFolder.empty() && std::filesystem::exists(savedFolder)) {
            logsFolder_ = savedFolder;
        }
    }

    if (logsFolder_.empty()) {
#ifdef _WIN32
        // Prompt user to select their WoW Logs folder
        logsFolder_ = showFolderPickerDialog(L"Select WoW Combat Log Folder");

        if (logsFolder_.empty()) {
            std::cerr << "No logs folder selected.\n";
            return false;
        }

        // Save the selected folder for next time
        SettingsCache::instance().get().overlayLogsFolder = logsFolder_.string();
        SettingsCache::instance().markDirty();
        SettingsCache::instance().flush();
#else
        std::cerr << "No logs folder specified. Use --logs <path> to specify.\n";
        return false;
#endif
    }

    // Wire up callbacks (shared with the settings popup's folder change)
    wireLogManagerCallbacks();

    // Start monitoring
    if (!logManager_->start(logsFolder_)) {
        return false;
    }

    // Attach stats to session
    if (auto* session = logManager_->getSession()) {
        stats_->attachSession(session);
    }

    std::cout << "Monitoring: " << logsFolder_.string() << "\n";
    return true;
}

void OverlayApplication::wireLogManagerCallbacks() {
    if (!logManager_) return;

    logManager_->setOnDataUpdate([this]() {
        // Refresh stats and update cached snapshot when new data arrives.
        // Rebuilding the stat databases mutates state the render thread
        // reads under tryLockActorMap, so hold the same lock while
        // swapping. Safe here: every onDataUpdate fires after the session
        // has released the lock.
        if (auto* session = logManager_->getSession()) {
            auto actorMapLock = session->lockActorMap();
            stats_->refresh();
        }
        if (auto* session = logManager_->getSession()) {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cachedSnapshot_ = session->getSnapshot();
        }
    });

    // No auto-freeze on pull end. LiveLogSession now defers clearing
    // ActorMap until the NEXT pull actually starts, so the live view
    // keeps showing the just-ended pull's numbers instead of blanking.
    // The user can still click into a specific historical pull via
    // the segment picker if they want to lock in on it. Deep-hopping
    // into HistoricalPull mode from a poll-thread callback also risks
    // re-entering the actorMapMutex_ that poll() holds, so leaving
    // this off avoids that class of bug.
}

void OverlayApplication::changeLogFolder(const std::filesystem::path& newFolder) {
    if (newFolder.empty()) return;

    std::error_code ec;
    if (!std::filesystem::is_directory(newFolder, ec)) return;

    // Nothing to do if the user re-picked the folder already being watched.
    if (!logsFolder_.empty() &&
        std::filesystem::equivalent(newFolder, logsFolder_, ec)) {
        return;
    }

    // Persist the choice so the next launch comes up on the new folder.
    SettingsCache::instance().get().overlayLogsFolder = newFolder.string();
    SettingsCache::instance().markDirty();
    SettingsCache::instance().flush();

    // Tear down the old watcher and stand a fresh one up on the new folder,
    // wiring the same callbacks and stats attachment the initial launch used
    // so the meter reattaches without a restart. Clear the stale snapshot so
    // the old folder's numbers don't linger while the new scan spins up.
    logManager_->stop();
    logsFolder_ = newFolder;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        cachedSnapshot_ = LiveLogSession::Snapshot{};
    }
    selectedSegment_ = SEGMENT_CURRENT;

    wireLogManagerCallbacks();

    if (!logManager_->start(logsFolder_)) {
        std::cerr << "Failed to start monitoring: " << logsFolder_.string() << "\n";
        return;
    }

    if (auto* session = logManager_->getSession()) {
        stats_->attachSession(session);
    }

    std::cout << "Monitoring: " << logsFolder_.string() << "\n";
}

void OverlayApplication::processFrame() {
    window_->pollEvents();

    // System-wide lock toggle. Runs outside any ImGui frame because it
    // just consumes a flag set by the keyboard hook. The Ctrl+arrow /
    // Ctrl+Shift+L overlay-focus keys need an active ImGui frame, so
    // they run from renderUI().
    pumpGlobalHotkeyMessages();

    // Begin Vulkan/ImGui frame
    if (!vulkan_->beginFrame()) {
        return;  // Skip frame if swapchain needs recreation
    }

    // Render ImGui UI (includes drag handling via ImGui)
    renderUI();

    // Submit and present
    vulkan_->renderImGui();
    vulkan_->endFrame();
}

#ifdef _WIN32
namespace {
    // We install a low-level keyboard hook so we can detect the global
    // lock-toggle chord (Ctrl+Shift+L) even when the game has keyboard
    // focus. RegisterHotKey posts WM_HOTKEY into the thread queue, but
    // GLFW's message pump drains that queue without forwarding the
    // message, so it never reaches us. LL_KEYBOARD_LL fires before any
    // window is involved.
    std::atomic<bool> g_hotkeyToggleRequested{false};
    HHOOK g_keyboardHook = nullptr;

    LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            if (kb && kb->vkCode == 'L') {
                bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                if (ctrl && shift) {
                    g_hotkeyToggleRequested.store(true);
                }
            }
        }
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }
}
#endif

void OverlayApplication::registerGlobalHotkeys() {
#ifdef _WIN32
    // WH_KEYBOARD_LL runs on this thread; the hook proc executes in the
    // context of whichever thread receives keyboard input, so we only
    // set a flag and let the main loop act on it. Failure is non-fatal.
    if (!g_keyboardHook) {
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc,
                                           GetModuleHandleW(nullptr), 0);
    }
#endif
}

void OverlayApplication::unregisterGlobalHotkeys() {
#ifdef _WIN32
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
#endif
}

void OverlayApplication::pumpGlobalHotkeyMessages() {
#ifdef _WIN32
    // Consume the flag set by the low-level keyboard hook.
    if (g_hotkeyToggleRequested.exchange(false)) {
        if (window_) {
            window_->setClickThrough(!window_->isClickThrough());
        }
    }
#endif
}

void OverlayApplication::pollLocalHotkeys() {
    // Called from renderUI while an ImGui frame is active so we can use
    // IsKeyPressed which handles debouncing for us. Works whenever the
    // overlay has keyboard focus.
    ImGuiIO& io = ImGui::GetIO();

    // Escape closes whichever drill-down modal is currently open. ImGui
    // doesn't wire this up for popup-modals, so we do it manually.
    // Priority order: actor breakdown, avoidance, death - only closes
    // one per press so back-to-back modals don't all disappear at once.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, /*repeat*/ false)) {
        if (breakdownPanel_ && breakdownPanel_->isOpen()) {
            breakdownPanel_->close();
        } else if (avoidanceBreakdownPanel_ && avoidanceBreakdownPanel_->isOpen()) {
            avoidanceBreakdownPanel_->close();
        } else if (deathRecapPanel_ && deathRecapPanel_->isOpen()) {
            deathRecapPanel_->close();
        }
    }

    // Ctrl+Left/Right cycles the current meter's view.
    if (!io.KeyCtrl) return;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, /*repeat*/ false)) {
        cycleMeterView(-1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, /*repeat*/ false)) {
        cycleMeterView(+1);
    }
    // Fallback lock toggle for users whose Ctrl+Shift+L is taken by
    // another app: Ctrl+Shift+L works when the overlay has focus.
    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        if (window_) window_->setClickThrough(!window_->isClickThrough());
    }
}

void OverlayApplication::cycleMeterView(int direction) {
    if (!meterPanel_) return;
    // Ordered list of the views we surface in the overlay. Matches the
    // per-panel dropdown so cycling and clicking end up on the same
    // things.
    static constexpr MeterViewType kCycleOrder[] = {
        MeterViewType::DamageDealt, MeterViewType::HealingDone,
        MeterViewType::DamageTaken, MeterViewType::DamageTakenBy,
        MeterViewType::FriendlyFire, MeterViewType::EnemyDamage,
        MeterViewType::Overhealing, MeterViewType::HealingTaken,
        MeterViewType::Absorbs, MeterViewType::EnemyHealing,
        MeterViewType::Dispels, MeterViewType::Interrupts,
        MeterViewType::CCBreaks, MeterViewType::Deaths,
        MeterViewType::Avoidance,
    };
    constexpr int count = static_cast<int>(sizeof(kCycleOrder) / sizeof(kCycleOrder[0]));

    auto& cfg = meterPanel_->getConfig();
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (kCycleOrder[i] == cfg.view_type) { idx = i; break; }
    }
    idx = (idx + direction + count) % count;
    cfg.view_type = kCycleOrder[idx];
}

void OverlayApplication::updateModalAutoGrow() {
    if (!window_) return;

    // Room for the outer overlay's header row (drag handle, staleness
    // dot, lock toggle, segment/view combos) plus a small side pad for
    // modal margins. Anything above the modal has to fit here.
    constexpr int kHeaderPad = 100;
    constexpr int kSidePad = 40;

    // Look at each panel's measured size from its last render frame.
    // Zero means "hasn't rendered yet" - use a generous first-open
    // fallback so the initial frame isn't cramped. Once the modal
    // renders once, subsequent frames use the actual laid-out size.
    int wantW = 0;
    int wantH = 0;
    auto want = [&](ImVec2 measured, int fallbackW, int fallbackH) {
        int w = measured.x > 0 ? static_cast<int>(measured.x) : fallbackW;
        int h = measured.y > 0 ? static_cast<int>(measured.y) : fallbackH;
        if (w > wantW) wantW = w;
        if (h > wantH) wantH = h;
    };

    // Actor Breakdown needs the widest layout: chart on top, spell
    // table (415px fixed columns + stretch Spell column + row scrollbar)
    // and a 280px sidebar side by side, plus header + footer. First
    // open uses 1600x1000 so nothing clips before the panel has been
    // measured; subsequent frames follow the true size.
    if (breakdownPanel_ && breakdownPanel_->isOpen()) {
        want(breakdownPanel_->getLastMeasuredSize(), 1600, 1000);
    }
    if (avoidanceBreakdownPanel_ && avoidanceBreakdownPanel_->isOpen()) {
        want(avoidanceBreakdownPanel_->getLastMeasuredSize(), 900, 650);
    }
    if (deathRecapPanel_ && deathRecapPanel_->isOpen()) {
        want(deathRecapPanel_->getLastMeasuredSize(), 900, 650);
    }
    if (mobWeightPanel_ && mobWeightPanel_->isVisible()) {
        want(mobWeightPanel_->getLastMeasuredSize(), 540, 500);
    }
    if (phaseEditorPanel_ && phaseEditorPanel_->isVisible()) {
        // Fixed target, not the live measured size: the editor window is
        // AlwaysAutoResize, so feeding its measured size back into the OS
        // grow made the two chase each other and jitter every frame. A
        // fixed roomy target lets the OS window settle once while the
        // auto-sizing editor floats inside it.
        want(ImVec2(0, 0), 620, 620);
    }
    if (settingsOpen_) {
        // Small popup; make sure the OS window has room even if the user
        // shrank it below the settings window's footprint.
        want(ImVec2(settingsMeasuredW_, settingsMeasuredH_), 420, 360);
    }

    bool wantsBig = (wantW > 0);

    // First transition: save current geometry.
    if (wantsBig && !modalGrowActive_) {
        window_->getPosition(savedWindowX_, savedWindowY_);
        window_->getSize(savedWindowW_, savedWindowH_);
        modalGrowActive_ = true;
    }

    if (wantsBig) {
        // Recompute target every frame so the OS window tracks the
        // modal size if the user drags to resize it.
        int newW = wantW + kSidePad;
        int newH = wantH + kHeaderPad;

        int newX = savedWindowX_;
        int newY = savedWindowY_;

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            int workX = 0, workY = 0, workW = 0, workH = 0;
            glfwGetMonitorWorkarea(monitor, &workX, &workY, &workW, &workH);
            if (workW > 0 && workH > 0) {
                // Cap at 95% of monitor work area.
                int maxW = (workW * 95) / 100;
                int maxH = (workH * 95) / 100;
                if (newW > maxW) newW = maxW;
                if (newH > maxH) newH = maxH;
                // Center each frame so growing the modal keeps it
                // visible on the primary monitor.
                newX = workX + (workW - newW) / 2;
                newY = workY + (workH - newH) / 2;
            }
        }

        // Only nudge the OS window when the size actually changed so
        // we're not fighting Windows over every frame.
        int curW = 0, curH = 0;
        window_->getSize(curW, curH);
        if (curW != newW || curH != newH) {
            window_->setSize(newW, newH);
            window_->setPosition(newX, newY);
        }
    } else if (modalGrowActive_) {
        // Transition: big -> small. Restore whatever the user had.
        if (savedWindowW_ > 0 && savedWindowH_ > 0) {
            window_->setSize(savedWindowW_, savedWindowH_);
            window_->setPosition(savedWindowX_, savedWindowY_);
        }
        modalGrowActive_ = false;
    }
}

void OverlayApplication::renderUI() {
    // Grow the OS window when a drill-down modal is open so the modal
    // isn't clamped to the small at-a-glance meter size. Runs before
    // the ImGui frame is set up so the resize propagates into
    // DisplaySize the same frame.
    updateModalAutoGrow();

    // Create fullscreen overlay window. NoBringToFrontOnFocus keeps this
    // background window from jumping over the modal panels (mob weights,
    // breakdowns, phase editor) when the user clicks the meter - those
    // panels are separate windows and must always stay on top once open.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##Overlay", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Overlay-focus hotkeys (Ctrl+Left/Right cycles views,
    // Ctrl+Shift+L fallback lock toggle). Runs here because
    // ImGui::IsKeyPressed needs an active frame context.
    pollLocalHotkeys();

    // Drag handle at top - invisible button that allows window dragging
    ImVec2 dragSize(ImGui::GetWindowWidth(), 16.0f);
    ImGui::InvisibleButton("##DragHandle", dragSize);

    // Drag against the absolute desktop cursor (see screenCursor). Both the
    // window position and the cursor anchor are captured once at click; each
    // frame the window moves by how far the desktop cursor travelled since.
    // Because the anchor is in desktop space, moving the window doesn't shift
    // it, so there's no feedback and no accumulating jitter.
    static int dragStartWinX = 0, dragStartWinY = 0;
    static double dragStartCurX = 0.0, dragStartCurY = 0.0;

    if (ImGui::IsItemActive()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            window_->getPosition(dragStartWinX, dragStartWinY);
            screenCursor(dragStartCurX, dragStartCurY);
        }

        double curX = 0.0, curY = 0.0;
        if (screenCursor(curX, curY)) {
            window_->setPosition(dragStartWinX + static_cast<int>(curX - dragStartCurX),
                                 dragStartWinY + static_cast<int>(curY - dragStartCurY));
        }
    }

    // Show drag cursor hint
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // Unified header row styling
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.22f, 0.22f, 0.26f, 1.0f));

    // Close button - Danger variant handles the destructive-color styling.
    if (awlui::Button("X", awlui::ButtonVariant::Danger,
                      awlui::ButtonSize::Sm, ImVec2(22, 22))) {
        glfwSetWindowShouldClose(window_->getWindow(), GLFW_TRUE);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Close Overlay");
    }

    ImGui::SameLine();

    // Staleness indicator (compact)
    auto secondsSinceWrite = logManager_ ? logManager_->getSecondsSinceLastWrite() : std::chrono::seconds(0);
    staleness_->renderCompact(secondsSinceWrite);

    ImGui::SameLine();

    // Lock/unlock toggle. Top row is intentionally minimal now:
    // [X close] [staleness dot] [Locked/Unlocked]. Segment and view
    // selectors live on the second row (rendered below, right above
    // the meter content) so the top bar never overflows.
    //
    // We also record the button's screen-space rect and hand it to
    // OverlayWindow so it stays clickable while the rest of the
    // overlay is passing clicks through to the game. This is the
    // escape hatch out of click-through mode.
    if (controls_ && window_) {
        bool isLocked = window_->isClickThrough();
        int btnRect[4] = {0, 0, 0, 0};
        controls_->renderLockToggle(isLocked, [this](bool locked) {
            if (window_) window_->setClickThrough(locked);
        }, btnRect);

        int winX = 0, winY = 0;
        window_->getPosition(winX, winY);
        window_->setInteractiveRect(
            btnRect[0] + winX, btnRect[1] + winY,
            btnRect[2] + winX, btnRect[3] + winY);
    }

    // Mob weighting editor toggle - live runs can tune weights without
    // switching back to the main app
    ImGui::SameLine();
    if (awlui::IconButton("mobweights", "%")) {
        if (mobWeightPanel_) mobWeightPanel_->toggleVisible();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("mobweight.title"));
    }

    // Phase editor button + selector, sitting next to the mob-weight
    // button on the top row. The meter body suppresses its own copy
    // (setPhaseControlsExternal) so these are the only ones shown. Only
    // appears for boss segments, which is when the panel has phases.
    if (meterPanel_ && meterPanel_->getShowPhaseControls()) {
        ImGui::SameLine();
        if (awlui::IconButton("overlay_phase_editor", "+")) {
            meterPanel_->requestPhaseEditor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("phases.title"));
        }

        if (meterPanel_->hasPhases()) {
            std::vector<const char*> phaseNames;
            phaseNames.reserve(meterPanel_->phaseCount() + 1);
            phaseNames.push_back(L("meter.phase_all"));
            for (int i = 0; i < meterPanel_->phaseCount(); ++i) {
                phaseNames.push_back(meterPanel_->phaseLabel(i));
            }
            int current = meterPanel_->getSelectedPhase() + 1;  // slot 0 = all
            ImGui::SameLine();
            ImGui::SetNextItemWidth(96.0f);
            if (awlui::Combo("##OverlayPhaseFilter", &current,
                             phaseNames.data(),
                             static_cast<int>(phaseNames.size()))) {
                meterPanel_->selectPhase(current - 1);
            }
        }
    }

    // Settings button - opens the popup with the log-folder and language
    // controls. The embedded Noto Sans subset has no gear glyph (U+2699),
    // so we use a plain ASCII label the font is guaranteed to render.
    ImGui::SameLine();
    if (awlui::IconButton("overlay_settings", "[=]")) {
        settingsOpen_ = !settingsOpen_;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("settings.title"));
    }

    ImGui::PopStyleColor(2);  // FrameBg colors
    ImGui::PopStyleVar(2);    // Rounding, padding

    ImGui::Spacing();

    // Second row: segment selector, drawn just before the meter content.
    // The panel's own view-type selector renders on the same line right
    // after this via SameLine at the top of renderEmbeddedContent.
    std::vector<PullSegment> pullHistory;
    PullSegment currentPull;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        pullHistory = cachedSnapshot_.pullHistory;
        currentPull = cachedSnapshot_.currentPull;
    }

    {
        // While the initial log scan runs, say so right in the segment
        // selector - segments stream into the list as they're found, so
        // the count ticking up doubles as a progress indicator
        bool scanningLog = false;
        if (logManager_) {
            if (auto* session = logManager_->getSession()) {
                scanningLog = session->isScanningSegments();
            }
        }

        if (scanningLog) {
            awlui::Spinner("");
            ImGui::SameLine(0, 4.0f);
        }

        float scopeWidth = 140.0f;
        ImGui::SetNextItemWidth(scopeWidth);

        char previewLabel[64] = "Current";
        if (scanningLog && selectedSegment_ == SEGMENT_CURRENT) {
            if (pullHistory.empty()) {
                snprintf(previewLabel, sizeof(previewLabel), "Scanning log...");
            } else {
                snprintf(previewLabel, sizeof(previewLabel), "Scanning... (%zu found)",
                    pullHistory.size());
            }
        } else if (selectedSegment_ == SEGMENT_CURRENT) {
            // The live view slides over whatever segment is active, so the
            // specific boss/trash label isn't meaningful here - just call
            // it Current.
            snprintf(previewLabel, sizeof(previewLabel), "> Current");
        } else if (selectedSegment_ < pullHistory.size()) {
            size_t displayNum = pullHistory.size() - selectedSegment_;
            snprintf(previewLabel, sizeof(previewLabel), "#%zu %s",
                displayNum, pullHistory[selectedSegment_].label.c_str());
        }

        if (ImGui::BeginCombo("##Segment", previewLabel)) {
            bool isCurrent = (selectedSegment_ == SEGMENT_CURRENT);
            if (ImGui::Selectable("> Current (Live)", isCurrent)) {
                selectedSegment_ = SEGMENT_CURRENT;
                viewMode_ = StatsViewMode::CurrentPull;
                stats_->clearHistoricalPullSelection();
            }

            if (!pullHistory.empty()) {
                ImGui::Separator();

                // Draw one flat, selectable segment row. Shared by both the
                // grouped M+ children and the standalone raid boss pulls.
                auto drawSegmentRow = [&](size_t idx, size_t displayNum) {
                    const auto& pull = pullHistory[idx];

                    ImVec4 color;
                    if (pull.segmentType == PullSegmentType::DungeonOverall) {
                        color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                    } else if (pull.segmentType == PullSegmentType::BossPull) {
                        color = ImVec4(1.0f, 0.84f, 0.0f, 1.0f);
                    } else {
                        color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Text, color);

                    char label[128];
                    snprintf(label, sizeof(label), "#%zu %s (%s)%s",
                        displayNum,
                        pull.label.c_str(),
                        pull.getDurationString().c_str(),
                        (pull.isEncounter && pull.success) ? " *" : "");

                    bool isSelected = (selectedSegment_ == idx);
                    if (ImGui::Selectable(label, isSelected)) {
                        selectedSegment_ = idx;
                        viewMode_ = StatsViewMode::HistoricalPull;
                        stats_->selectHistoricalPull(idx);
                    }

                    ImGui::PopStyleColor();
                };

                // Walk newest-first. M+ segments (dungeonName set) collapse
                // under one header per dungeonRunId; standalone raid boss
                // pulls render flat, no wrapper. Children keep newest-at-top
                // order within their run. Parsing stays lazy - a child only
                // parses when its row is clicked (selectHistoricalPull).
                size_t displayNum = pullHistory.size();
                for (size_t i = 0; i < pullHistory.size(); ) {
                    size_t idx = pullHistory.size() - 1 - i;
                    const auto& head = pullHistory[idx];

                    if (head.dungeonName.empty()) {
                        // Ungrouped (raid boss / trash without a run)
                        drawSegmentRow(idx, displayNum--);
                        ++i;
                        continue;
                    }

                    // Collect this run's contiguous span (walking backwards,
                    // all rows sharing dungeonRunId sit together).
                    uint32_t runId = head.dungeonRunId;
                    size_t runCount = 0;
                    while (i + runCount < pullHistory.size()) {
                        size_t j = pullHistory.size() - 1 - (i + runCount);
                        if (pullHistory[j].dungeonRunId != runId ||
                            pullHistory[j].dungeonName.empty()) {
                            break;
                        }
                        ++runCount;
                    }

                    // Group header: "Algeth'ar Academy +12 (12:37)". The run
                    // duration comes from dungeonEndTime_ms (0 while live -> "...").
                    char header[160];
                    int32_t runDur = (head.dungeonEndTime_ms > head.dungeonStartTime_ms)
                        ? head.dungeonEndTime_ms - head.dungeonStartTime_ms : 0;
                    if (runDur > 0) {
                        int mins = runDur / 60000, secs = (runDur / 1000) % 60;
                        if (head.keystoneLevel > 0) {
                            snprintf(header, sizeof(header), "%s +%u (%d:%02d)###run%u",
                                head.dungeonName.c_str(), head.keystoneLevel, mins, secs, runId);
                        } else {
                            snprintf(header, sizeof(header), "%s (%d:%02d)###run%u",
                                head.dungeonName.c_str(), mins, secs, runId);
                        }
                    } else if (head.keystoneLevel > 0) {
                        snprintf(header, sizeof(header), "%s +%u###run%u",
                            head.dungeonName.c_str(), head.keystoneLevel, runId);
                    } else {
                        snprintf(header, sizeof(header), "%s###run%u",
                            head.dungeonName.c_str(), runId);
                    }

                    // Newest run defaults open so the last pull is one click away.
                    ImGui::SetNextItemOpen(i == 0, ImGuiCond_Once);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
                    bool open = ImGui::TreeNode(header);
                    ImGui::PopStyleColor();
                    if (open) {
                        for (size_t k = 0; k < runCount; ++k) {
                            size_t childIdx = pullHistory.size() - 1 - (i + k);
                            drawSegmentRow(childIdx, displayNum - k);
                        }
                        ImGui::TreePop();
                    }
                    displayNum -= runCount;
                    i += runCount;
                }
            }

            ImGui::EndCombo();
        }
        ImGui::SameLine();  // put the panel's view combo on the same row
    }

    // Refresh class-color spec ids and the selected segment's phase
    // list before the meter renders. Both read the snapshot under their
    // own locks, so they're safe outside the ActorMap lock below.
    syncSpecColors();
    updatePhases();

    // Meter panel content (embedded, no separate window)
    if (stats_ && logManager_) {
        auto* session = logManager_->getSession();
        if (session) {
            // Show a spinner instead of a bare "Updating..." string so
            // the user can tell we're chewing on the log (segment
            // re-parse, mid-fight poll) rather than frozen. Skips the
            // meter render path entirely because the ActorMap is being
            // mutated on another thread.
            if (session->isParsingInProgress()) {
                awlui::Spinner("Parsing...");
            } else {
                // Try to acquire lock on ActorMap for safe iteration
                auto actorMapLock = session->tryLockActorMap();
                if (!actorMapLock.owns_lock()) {
                    // Lock not acquired (parsing started between flag check and lock attempt)
                    awlui::Spinner("Parsing...");
                } else {
                    // Check if we have segments but no combat data loaded yet
                    // This happens after startup scan - segments are detected but data
                    // is only parsed on-demand when user selects a segment
                    const auto* combatDb = stats_->getCombatDatabase();
                    bool hasSegments = !pullHistory.empty() || !currentPull.label.empty();
                    bool hasCombatData = combatDb && !combatDb->empty();

                    if (hasSegments && !hasCombatData && selectedSegment_ == SEGMENT_CURRENT) {
                        // Segments found but no data loaded - prompt user to select
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Select a segment to view data");
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "or wait for new combat events");
                    } else {
                        // Get time range for current view mode
                        auto [startTime, endTime] = stats_->getTimeRange(viewMode_);
                        uint32_t currentTime_ms = static_cast<uint32_t>(std::max(0, endTime));

                        // Make a thread-safe local copy of guidToName and
                        // spellIdToName. Both are needed by the meter render
                        // path - spell names come straight from the combat
                        // log's own name field, so drill-downs read correctly
                        // in the client's locale without needing a
                        // SpellDataTable binary loaded in overlay mode.
                        std::unordered_map<std::string, std::string> guidToNameCopy;
                        std::unordered_map<uint32_t, std::string> spellIdToNameCopy;
                        {
                            std::lock_guard<std::mutex> lock(snapshotMutex_);
                            guidToNameCopy = cachedSnapshot_.guidToName;
                            spellIdToNameCopy = cachedSnapshot_.spellIdToName;
                        }

                        // Build string_view map from guidToNameCopy for render functions
                        std::unordered_map<std::string_view, std::string_view> guidToNameView;
                        for (const auto& [guid, name] : guidToNameCopy) {
                            guidToNameView.emplace(std::string_view(guid), std::string_view(name));
                        }

                        // Render meter content directly (skip tab bar and time mode in overlay)
                        meterPanel_->renderEmbeddedContent(
                            combatDb,
                            stats_->getDispelDatabase(),
                            colorGen_,
                            guidToNameView,
                            currentTime_ms,
                            nullptr,  // iconLoader
                            &guidToNameCopy,  // Fallback for names not in position events
                            stats_->getDeathDatabase(),
                            stats_->getResourceDatabase(),
                            stats_->getAuraDatabase(),
                            stats_->getAbsorbDatabase(),
                            stats_->getAvoidanceDatabase(),
                            startTime,  // Filter start time for segment filtering
                            endTime,    // Filter end time for segment filtering
                            &spellIdToNameCopy  // Overlay spell-name fallback
                        );

                        // Handle death / avoidance row clicks. Both meters
                        // set clickedActor_ (not breakdownRequest_) so we
                        // route them here rather than through the actor
                        // breakdown panel path below.
                        if (auto clickedGuid = meterPanel_->getClickedActor()) {
                            auto viewType = meterPanel_->getConfig().view_type;

                            if (viewType == MeterViewType::Deaths && deathRecapPanel_) {
                                auto* deathDb = stats_->getDeathDatabase();
                                if (deathDb) {
                                    // Collect every death recorded for this
                                    // actor - the new modal lists all of
                                    // them on the left pane so users can
                                    // switch between deaths without
                                    // reopening.
                                    auto deathPtrs = deathDb->getPlayerDeaths();
                                    std::vector<DeathEvent> actorDeaths;
                                    std::string actorName;
                                    for (const auto* death : deathPtrs) {
                                        if (death->actor_guid == *clickedGuid) {
                                            if (actorName.empty()) actorName = death->actor_name;
                                            actorDeaths.push_back(*death);
                                        }
                                    }
                                    if (actorName.empty()) {
                                        auto it = guidToNameCopy.find(*clickedGuid);
                                        if (it != guidToNameCopy.end()) actorName = it->second;
                                    }
                                    deathRecapPanel_->open(
                                        *clickedGuid, actorName, std::move(actorDeaths));
                                }
                            }
                            else if (viewType == MeterViewType::Avoidance && avoidanceBreakdownPanel_) {
                                auto* avoidanceDb = stats_->getAvoidanceDatabase();
                                if (avoidanceDb && !avoidanceDb->empty()) {
                                    int32_t dbMin = avoidanceDb->getMinTimestamp();
                                    int32_t dbMax = avoidanceDb->getMaxTimestamp();
                                    int32_t windowStart = startTime > 0 ? startTime : dbMin;
                                    int32_t windowEnd = (endTime > 0 && endTime != INT32_MAX)
                                        ? endTime : dbMax;
                                    auto breakdown = avoidanceDb->getBreakdownForTarget(
                                        *clickedGuid, windowStart, windowEnd);

                                    // Resolve a display name for the modal header.
                                    std::string targetName;
                                    auto nameIt = guidToNameCopy.find(*clickedGuid);
                                    if (nameIt != guidToNameCopy.end()) {
                                        targetName = nameIt->second;
                                    } else {
                                        targetName = clickedGuid->substr(0, 16);
                                    }
                                    uint32_t windowMs = (windowEnd > windowStart)
                                        ? static_cast<uint32_t>(windowEnd - windowStart)
                                        : 0;
                                    avoidanceBreakdownPanel_->open(
                                        *clickedGuid, targetName, breakdown, windowMs);
                                }
                            }
                            meterPanel_->clearClickedActor();
                        }

                        // Handle actor row click to open the full breakdown
                        // panel - same chart + spell table + drag-to-select
                        // time filter as the main app. Metric picked from
                        // the meter's current view so damage/heal/taken all
                        // route to the right query.
                        if (auto request = meterPanel_->getBreakdownRequest()) {
                            if (breakdownPanel_) {
                                CombatMetricType metricType;
                                switch (meterPanel_->getConfig().view_type) {
                                    case MeterViewType::HealingDone:
                                    case MeterViewType::Overhealing:
                                        metricType = CombatMetricType::HealingDone;
                                        break;
                                    case MeterViewType::HealingTaken:
                                        // "who healed X" - synthesizes a
                                        // per-spell breakdown from every
                                        // source that healed this actor.
                                        metricType = CombatMetricType::HealingReceived;
                                        break;
                                    case MeterViewType::DamageTaken:
                                    case MeterViewType::DamageTakenByAbility:
                                        metricType = CombatMetricType::DamageTaken;
                                        break;
                                    case MeterViewType::DamageTakenBy:
                                        // Rows here are players ranked by the
                                        // damage they DID to the selected enemy,
                                        // so their drill-down is damage dealt -
                                        // their abilities against that target -
                                        // not the damage they took.
                                        metricType = CombatMetricType::DamageDealt;
                                        break;
                                    case MeterViewType::FriendlyFire:
                                        // Restrict the actor's damage
                                        // breakdown to hits on player
                                        // targets so the drill-down only
                                        // lists FF-relevant spells.
                                        metricType = CombatMetricType::FriendlyFire;
                                        break;
                                    default:
                                        metricType = CombatMetricType::DamageDealt;
                                        break;
                                }
                                breakdownPanel_->open(request->actorGuid, metricType);
                            }
                            meterPanel_->clearBreakdownRequest();
                        }

                        // Render death recap modal (if open)
                        if (deathRecapPanel_) {
                            deathRecapPanel_->render(
                                combatDb,
                                stats_->getResourceDatabase(),
                                colorGen_,
                                guidToNameView,
                                nullptr,  // iconLoader (not available in overlay)
                                &guidToNameCopy,
                                &spellIdToNameCopy
                            );
                        }

                        // Render actor breakdown modal (if open). Same
                        // chart/table renderers the main app uses; the
                        // drag-to-select on the graph provides the time
                        // filter internally.
                        if (breakdownPanel_) {
                            breakdownPanel_->render(
                                combatDb,
                                colorGen_,
                                guidToNameView,
                                nullptr,  // iconLoader (not available in overlay)
                                currentTime_ms,
                                /*useFullEncounter=*/true,
                                &spellIdToNameCopy
                            );
                        }

                        // Render avoidance drill-down modal (if open).
                        // Uses the pre-computed AvoidanceBreakdown so it
                        // survives DB refreshes between frames.
                        if (avoidanceBreakdownPanel_) {
                            avoidanceBreakdownPanel_->render(
                                guidToNameView,
                                nullptr,  // iconLoader
                                &guidToNameCopy,
                                &spellIdToNameCopy
                            );
                        }
                    }
                }
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting for combat log...");
        }
    }

    // Mob weighting editor - same panel the main app uses, fed from the
    // live combat database. Rendered outside the meter block so it stays
    // usable while a parse is in flight.
    if (mobWeightPanel_ && mobWeightPanel_->isVisible() && stats_) {
        std::unordered_map<std::string, std::string> weightGuidToName;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            weightGuidToName = cachedSnapshot_.guidToName;
        }
        mobWeightPanel_->render(stats_->getCombatDatabase(), &weightGuidToName);

        if (mobWeightPanel_->consumeWeightsChanged()) {
            if (const auto* db = stats_->getCombatDatabase()) {
                db->refreshTargetWeights();
            }
            // The meter caches its ranked stats, so nudge it to re-aggregate
            // with the new weights this frame rather than on the next tick.
            if (meterPanel_) meterPanel_->invalidateStatsCache();
            // Write through immediately so the main app sees the edit
            // and nothing is lost if the overlay is closed abruptly
            SettingsCache::instance().flush();
        }
    }

    // Boss phase editor - the meter's "+" button toggles it. Only
    // reachable for boss segments (the phase controls are hidden
    // otherwise, so the button can't be clicked).
    if (meterPanel_ && phaseEditorPanel_) {
        if (meterPanel_->consumePhaseEditorRequest()) {
            phaseEditorPanel_->toggleVisible();
        }
    }

    // "Starts a new phase here" from the breakdown table's row menu
    // funnels through SpellContextMenu just like the main app. Apply
    // it to the current boss encounter and persist.
    if (SpellContextMenu::hasPendingPhaseRequest()) {
        auto phaseRequest = SpellContextMenu::consumePhaseRequest();
        if (currentPhaseEncounterId_ != 0 && phaseRequest.spellId != 0) {
            if (phaseRequest.add) {
                PhaseSettings::instance().addRule(currentPhaseEncounterId_, phaseRequest.spellId);
            } else {
                PhaseSettings::instance().removeRule(currentPhaseEncounterId_, phaseRequest.spellId);
            }
            auto& cache = SettingsCache::instance();
            cache.get().phaseSettingsJson = PhaseSettings::instance().toJson();
            cache.markDirty();
            cache.flush();
        }
    }

    if (phaseEditorPanel_ && phaseEditorPanel_->isVisible() && stats_) {
        PhaseEditorData editorData = buildPhaseEditorData();
        phaseEditorPanel_->render(editorData);
        if (phaseEditorPanel_->consumeRulesChanged()) {
            // Edits are already saved by the panel; write through so the
            // main app sees them and rebuild happens next frame via
            // updatePhases()
            SettingsCache::instance().flush();
        }
    }

    // Settings popup - log folder + language picker. Same top-of-z-order
    // treatment as the other panels since the meter window doesn't bring
    // itself to front on focus.
    renderSettingsWindow();

    // Drag the overlay from any empty spot, not just the thin strip up
    // top - during the initial scan the strip is the only handle and
    // everyone misses it, which reads as a frozen window. Runs after
    // every item has been submitted so the hover checks are accurate;
    // a drag that starts on a control (combo, button, graph) is left
    // alone.
    {
        static bool draggingFromBody = false;
        static int bodyDragWinX = 0, bodyDragWinY = 0;
        static double bodyDragCurX = 0.0, bodyDragCurY = 0.0;

        if (!draggingFromBody) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
                draggingFromBody = true;
                window_->getPosition(bodyDragWinX, bodyDragWinY);
                screenCursor(bodyDragCurX, bodyDragCurY);
            }
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Move by how far the desktop cursor travelled since the click,
            // in absolute desktop space, so moving the window can't feed
            // back into the delta (see the drag-handle note above).
            double curX = 0.0, curY = 0.0;
            if (screenCursor(curX, curY)) {
                window_->setPosition(bodyDragWinX + static_cast<int>(curX - bodyDragCurX),
                                     bodyDragWinY + static_cast<int>(curY - bodyDragCurY));
            }
        } else {
            draggingFromBody = false;
        }
    }

    ImGui::End();
}

void OverlayApplication::renderSettingsWindow() {
    if (!settingsOpen_) return;

    // The languages the overlay can actually render. The built-in font
    // covers Latin + Cyrillic only, so Korean and Chinese are left off
    // (they'd draw as empty boxes). Each entry shows its native name.
    struct LanguageChoice {
        Locale locale;
        const char* nativeName;
    };
    // Native names as UTF-8. Hex escapes are split into separate string
    // literals where a letter follows (e.g. "...\xA7" "ais") so the
    // compiler doesn't fold the trailing letter into the hex escape.
    static const LanguageChoice kLanguages[] = {
        {Locale::en_US, "English"},
        {Locale::de_DE, "Deutsch"},
        {Locale::fr_FR, "Fran\xC3\xA7" "ais"},       // Français
        {Locale::es_MX, "Espa\xC3\xB1" "ol"},        // Español
        {Locale::pt_BR, "Portugu\xC3\xAA" "s"},      // Português
        {Locale::ru_RU, "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"},  // Русский
    };
    constexpr int kLanguageCount = static_cast<int>(sizeof(kLanguages) / sizeof(kLanguages[0]));

    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(L("settings.title"), &settingsOpen_,
                     ImGuiWindowFlags_NoCollapse)) {
        // --- Log folder ---
        ImGui::TextDisabled("%s", L("settings.log_folder"));
        std::string folderText = logsFolder_.empty()
            ? std::string("-")
            : logsFolder_.string();
        ImGui::TextWrapped("%s", folderText.c_str());

        if (awlui::Button(L("settings.change_log_folder"),
                          awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Sm)) {
#ifdef _WIN32
            std::filesystem::path picked =
                showFolderPickerDialog(L"Select WoW Combat Log Folder");
            // A cancelled picker or an invalid/same folder is a no-op.
            changeLogFolder(picked);
#endif
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Language ---
        ImGui::TextDisabled("%s", L("settings.language"));

        int current = 0;
        Locale active = LocalizationManager::instance().getLocale();
        for (int i = 0; i < kLanguageCount; ++i) {
            if (kLanguages[i].locale == active) {
                current = i;
                break;
            }
        }

        const char* items[kLanguageCount];
        for (int i = 0; i < kLanguageCount; ++i) {
            items[i] = kLanguages[i].nativeName;
        }

        ImGui::SetNextItemWidth(200.0f);
        if (awlui::Combo("##OverlayLanguage", &current, items, kLanguageCount)) {
            Locale chosen = kLanguages[current].locale;
            LocalizationManager::instance().setLocale(chosen);
            // Persist the manual choice so it survives a restart.
            SettingsCache::instance().get().locale =
                LocalizationManager::getLocaleCode(chosen);
            SettingsCache::instance().markDirty();
            SettingsCache::instance().flush();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (awlui::Button(L("btn.close"), awlui::ButtonVariant::Ghost,
                          awlui::ButtonSize::Sm)) {
            settingsOpen_ = false;
        }

        // Laid-out size for the overlay's window auto-grow.
        ImVec2 sz = ImGui::GetWindowSize();
        settingsMeasuredW_ = sz.x;
        settingsMeasuredH_ = sz.y;
    }
    ImGui::End();
}

bool OverlayApplication::screenCursor(double& x, double& y) const {
    // Absolute desktop cursor position: window origin + cursor within the
    // client area. This is invariant while dragging - as the window moves
    // under a still mouse, the window origin rises by exactly what the
    // in-client cursor falls, so the sum doesn't move. Anchoring a drag to
    // this instead of ImGui's window-relative GetMousePos (which shifts
    // with the window and feeds back) is what stops the drag jitter.
    if (!window_ || !window_->getWindow()) return false;
    int winX = 0, winY = 0;
    window_->getPosition(winX, winY);
    double curX = 0.0, curY = 0.0;
    glfwGetCursorPos(window_->getWindow(), &curX, &curY);
    x = winX + curX;
    y = winY + curY;
    return true;
}

void OverlayApplication::syncSpecColors() {
    // Copy any new player specs out of the snapshot into the persistent
    // store, then hand the store's stable string_views to the color
    // generator. Doing this every frame is cheap (a handful of players)
    // and idempotent; cacheSpecId ignores repeats.
    std::unordered_map<std::string, uint16_t> specs;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        specs = cachedSnapshot_.guidToSpecId;
    }
    for (const auto& [guid, specId] : specs) {
        auto [it, inserted] = specIdStore_.try_emplace(guid, specId);
        if (inserted || it->second != specId) {
            it->second = specId;
        }
        colorGen_->cacheSpecId(std::string_view(it->first), it->second);
    }
}

void OverlayApplication::updatePhases() {
    if (!meterPanel_) return;

    // Resolve the selected segment's encounter id and pull window.
    // Phases only make sense on boss pulls (a segment carrying an
    // ENCOUNTER_START id); trash / M+ trash segments hide the phase UI.
    PullSegment selected;
    bool haveSegment = false;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        if (selectedSegment_ == SEGMENT_CURRENT) {
            selected = cachedSnapshot_.currentPull;
            haveSegment = true;
        } else if (selectedSegment_ < cachedSnapshot_.pullHistory.size()) {
            selected = cachedSnapshot_.pullHistory[selectedSegment_];
            haveSegment = true;
        }
    }

    uint32_t encounterId = (haveSegment && selected.isEncounter) ? selected.encounterId : 0;
    currentPhaseEncounterId_ = encounterId;
    // The breakdown context menu's "starts a new phase here" reads the
    // encounter id from here.
    SpellContextMenu::setCurrentEncounterId(encounterId);

    if (encounterId == 0) {
        meterPanel_->setShowPhaseControls(false);
        meterPanel_->setPhases({});
        return;
    }
    meterPanel_->setShowPhaseControls(true);

    // The meter window the overlay filters is on the same clock the
    // live capture uses (log-relative for the current pull, segment-
    // relative for a re-parsed historical pull). getTimeRange gives
    // that window for the active view mode.
    auto [startTime, endTime] = stats_->getTimeRange(viewMode_);
    currentPullStart_ms_ = startTime;
    currentPullEnd_ms_ = (endTime == INT32_MAX)
        ? (stats_->getCombatDatabase() ? stats_->getCombatDatabase()->getMaxTimestamp() : startTime)
        : endTime;

    const auto* rules = PhaseSettings::instance().rulesFor(encounterId);
    if (!rules) {
        meterPanel_->setPhases({});
        return;
    }

    // Live capture: first casts by spell id, emotes by cleaned text.
    std::unordered_map<uint32_t, LiveFirstCast> firstCasts;
    std::vector<EmoteEvent> emotes;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        firstCasts = cachedSnapshot_.firstCasts;
        emotes = cachedSnapshot_.emotes;
    }

    phase::RuleInputs inputs;
    inputs.pullStart_ms = currentPullStart_ms_;
    inputs.pullEnd_ms = currentPullEnd_ms_;
    inputs.firstCastTime = [&firstCasts](uint32_t spellId) -> std::optional<int32_t> {
        auto it = firstCasts.find(spellId);
        if (it != firstCasts.end()) return it->second.time_ms;
        return std::nullopt;
    };
    inputs.firstEmoteTime = [&emotes](std::string_view text) -> std::optional<int32_t> {
        for (const auto& e : emotes) {
            if (e.text == text) return e.timestamp_ms;
        }
        return std::nullopt;
    };

    auto resolved = phase::resolvePhases(*rules, inputs);
    std::vector<MeterPhase> phases;
    phases.reserve(resolved.size());
    for (auto& p : resolved) {
        phases.push_back(MeterPhase{std::move(p.label), p.start_ms, p.end_ms});
    }
    meterPanel_->setPhases(std::move(phases));
}

PhaseEditorData OverlayApplication::buildPhaseEditorData() {
    PhaseEditorData data;
    data.encounterId = currentPhaseEncounterId_;
    data.ruleInputs.pullStart_ms = currentPullStart_ms_;
    data.ruleInputs.pullEnd_ms = currentPullEnd_ms_;

    std::unordered_map<uint32_t, LiveFirstCast> firstCasts;
    std::vector<EmoteEvent> emotes;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        firstCasts = cachedSnapshot_.firstCasts;
        emotes = cachedSnapshot_.emotes;
    }

    // Resolvers so the "-> 2:30 / not reached" column works
    data.ruleInputs.firstCastTime = [firstCasts](uint32_t spellId) -> std::optional<int32_t> {
        auto it = firstCasts.find(spellId);
        if (it != firstCasts.end()) return it->second.time_ms;
        return std::nullopt;
    };
    data.ruleInputs.firstEmoteTime = [emotes](std::string_view text) -> std::optional<int32_t> {
        for (const auto& e : emotes) {
            if (e.text == text) return e.timestamp_ms;
        }
        return std::nullopt;
    };

    // Distinct hostile casts, time-sorted, as candidate split points
    for (const auto& [spellId, cast] : firstCasts) {
        if (cast.hostile_source) {
            data.casts.push_back({spellId, cast.time_ms, cast.spell_name});
        }
    }
    std::sort(data.casts.begin(), data.casts.end(),
              [](const PhaseEditorData::CastRow& a, const PhaseEditorData::CastRow& b) {
                  return a.time_ms < b.time_ms;
              });

    // Emote rows, time-sorted (the snapshot copy is already sorted)
    for (const auto& e : emotes) {
        data.emotes.push_back({e.timestamp_ms, e.source_name, e.text});
    }

    return data;
}

void OverlayApplication::loadSettings() {
    // Mob weighting is configured in the main app; the overlay reads
    // the saved weights so live meters discount the same mobs. The
    // weight cache rebuilds on every stats refresh, so this only needs
    // to happen once at launch.
    MobWeightSettings::instance().fromJson(
        SettingsCache::instance().get().mobWeightSettingsJson);

    // Boss phase rules are per-encounter and shared with the main app
    // through the same settings file, so the overlay loads them the
    // same way. Edits flush back through SettingsCache::flush().
    PhaseSettings::instance().fromJson(
        SettingsCache::instance().get().phaseSettingsJson);
}

void OverlayApplication::saveSettings() {
    // Save overlay-specific settings
}

// Helper functions for command-line parsing

bool isOverlayModeRequested(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--overlay") == 0 || strcmp(argv[i], "-o") == 0) {
            return true;
        }
    }
    return false;
}

bool isOverlayModeRequested(int argc, wchar_t* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--overlay") == 0 || wcscmp(argv[i], L"-o") == 0) {
            return true;
        }
    }
    return false;
}

std::filesystem::path getLogsFolderFromArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--logs") == 0 || strcmp(argv[i], "-l") == 0) {
            return std::filesystem::path(argv[i + 1]);
        }
    }
    return {};
}

std::filesystem::path getLogsFolderFromArgs(int argc, wchar_t* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (wcscmp(argv[i], L"--logs") == 0 || wcscmp(argv[i], L"-l") == 0) {
            return std::filesystem::path(argv[i + 1]);
        }
    }
    return {};
}
