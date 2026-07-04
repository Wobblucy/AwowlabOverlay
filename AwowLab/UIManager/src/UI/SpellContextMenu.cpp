#include "UI/SpellContextMenu.h"
#include "UI/SpellLinkOpener.h"
#include "Core/LocalizationManager.h"
#include "Core/PhaseSettings.h"
#include <algorithm>

// Static member definitions
bool SpellContextMenu::s_popupOpen = false;
bool SpellContextMenu::s_popupJustOpened = false;
ImVec2 SpellContextMenu::s_popupPos = ImVec2(0, 0);

uint32_t SpellContextMenu::s_spellId = 0;
std::string SpellContextMenu::s_spellName;
int32_t SpellContextMenu::s_timestampMs = -1;
bool SpellContextMenu::s_isHostile = false;
std::vector<int32_t> SpellContextMenu::s_groupedTimestamps;
std::string SpellContextMenu::s_actorGuid;

SpellContextMenu::NavigationRequest SpellContextMenu::s_pendingNavigation;
bool SpellContextMenu::s_pendingRaidNoteRequest = false;

uint32_t SpellContextMenu::s_currentEncounterId = 0;
SpellContextMenu::PhaseRequest SpellContextMenu::s_pendingPhaseRequest;
bool SpellContextMenu::s_hasPendingPhaseRequest = false;

bool SpellContextMenu::checkAndShow(uint32_t spellId, const std::string& spellName,
                                     int32_t timestamp_ms, bool isHostile,
                                     const std::vector<int32_t>& groupedTimestamps,
                                     std::string_view actorGuid) {
    // Don't trigger if popup is already open or if any other popup is active
    if (s_popupOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return false;
    }

    // Check for right-click on the last ImGui item
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        s_spellId = spellId;
        s_spellName = spellName;
        s_timestampMs = timestamp_ms;
        s_isHostile = isHostile;
        s_groupedTimestamps = groupedTimestamps;
        s_actorGuid = std::string(actorGuid);
        s_popupPos = ImGui::GetMousePos();
        s_popupOpen = true;
        s_popupJustOpened = true;
        return true;
    }
    return false;
}

bool SpellContextMenu::checkAndShowRect(uint32_t spellId, const std::string& spellName,
                                         int32_t timestamp_ms, bool isHostile,
                                         ImVec2 min, ImVec2 max,
                                         const std::vector<int32_t>& groupedTimestamps,
                                         std::string_view actorGuid) {
    // Don't trigger if popup is already open or if any other popup is active
    if (s_popupOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        return false;
    }

    ImVec2 mousePos = ImGui::GetMousePos();

    // Check if mouse is within rect and right-clicked
    if (mousePos.x >= min.x && mousePos.x <= max.x &&
        mousePos.y >= min.y && mousePos.y <= max.y &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {

        s_spellId = spellId;
        s_spellName = spellName;
        s_timestampMs = timestamp_ms;
        s_isHostile = isHostile;
        s_groupedTimestamps = groupedTimestamps;
        s_actorGuid = std::string(actorGuid);
        s_popupPos = mousePos;
        s_popupOpen = true;
        s_popupJustOpened = true;
        return true;
    }
    return false;
}

void SpellContextMenu::renderPopup() {
    if (!s_popupOpen) {
        return;
    }

    // Position popup at mouse cursor on first frame, clamped to viewport
    if (s_popupJustOpened) {
        // Estimate popup size (width for longest menu item, height for items + header)
        const float estimatedWidth = 220.0f;
        const float estimatedHeight = 140.0f;

        // Get viewport bounds
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 workPos = viewport->WorkPos;
        ImVec2 workSize = viewport->WorkSize;

        // Clamp position to keep popup within viewport
        ImVec2 clampedPos;
        clampedPos.x = std::min(s_popupPos.x, workPos.x + workSize.x - estimatedWidth);
        clampedPos.y = std::min(s_popupPos.y, workPos.y + workSize.y - estimatedHeight);
        clampedPos.x = std::max(clampedPos.x, workPos.x);
        clampedPos.y = std::max(clampedPos.y, workPos.y);

        ImGui::SetNextWindowPos(clampedPos);
        ImGui::SetNextWindowFocus();
        ImGui::OpenPopup("##SpellContextMenu");
        s_popupJustOpened = false;
    }

    if (ImGui::BeginPopup("##SpellContextMenu", ImGuiWindowFlags_NoSavedSettings)) {
        // Header: spell name and ID
        ImGui::TextUnformatted(s_spellName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%u)", s_spellId);
        ImGui::Separator();

        // Option 1: Open on Wowhead (only when a link opener is registered;
        // offline builds like the overlay have none, so the entry is hidden)
        if (const auto& openSpellPage = ui::spellLinkOpener()) {
            if (ImGui::MenuItem(L("context_menu.open_wowhead"), "Ctrl+LMB")) {
                openSpellPage(s_spellId);
                s_popupOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }

        // Option 2: Open spell settings (smart routing based on isHostile)
        if (ImGui::MenuItem(L("context_menu.open_spell_settings"))) {
            s_pendingNavigation.target = NavigationRequest::Target::SpellSettings;
            s_pendingNavigation.spellId = s_spellId;
            s_pendingNavigation.isHostile = s_isHostile;
            s_pendingNavigation.actorGuid = s_actorGuid;
            s_popupOpen = false;
            ImGui::CloseCurrentPopup();
        }

        // Option 3: Open in Auto Annotations (boss/hostile spells only)
        if (s_isHostile) {
            if (ImGui::MenuItem(L("context_menu.open_auto_annotations"))) {
                s_pendingNavigation.target = NavigationRequest::Target::AutoAnnotations;
                s_pendingNavigation.spellId = s_spellId;
                s_pendingNavigation.isHostile = s_isHostile;
                s_popupOpen = false;
                ImGui::CloseCurrentPopup();
            }

            // Phase trigger toggle: the first cast of the marked spell
            // starts a new phase in the meters for this encounter
            if (s_currentEncounterId != 0) {
                bool isTrigger = PhaseSettings::instance().hasRule(s_currentEncounterId, s_spellId);
                const char* label = isTrigger ? L("context_menu.remove_phase_trigger")
                                              : L("context_menu.set_phase_trigger");
                if (ImGui::MenuItem(label)) {
                    s_pendingPhaseRequest = PhaseRequest{s_spellId, !isTrigger};
                    s_hasPendingPhaseRequest = true;
                    s_popupOpen = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        // Option 4: Add to raid note
        if (ImGui::MenuItem(L("context_menu.add_to_raid_note"))) {
            s_pendingRaidNoteRequest = true;
            s_popupOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    } else {
        // Popup was closed (clicked outside)
        s_popupOpen = false;
    }
}

SpellContextMenu::NavigationRequest SpellContextMenu::consumePendingNavigation() {
    NavigationRequest request = s_pendingNavigation;
    s_pendingNavigation = NavigationRequest{};
    return request;
}

bool SpellContextMenu::hasPendingRaidNoteRequest() {
    return s_pendingRaidNoteRequest;
}

void SpellContextMenu::consumeRaidNoteRequest() {
    s_pendingRaidNoteRequest = false;
}

bool SpellContextMenu::hasPendingPhaseRequest() {
    return s_hasPendingPhaseRequest;
}

SpellContextMenu::PhaseRequest SpellContextMenu::consumePhaseRequest() {
    s_hasPendingPhaseRequest = false;
    PhaseRequest request = s_pendingPhaseRequest;
    s_pendingPhaseRequest = PhaseRequest{};
    return request;
}
