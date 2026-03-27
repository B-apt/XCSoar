// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InfoBoxes/Content/Alternate.hpp"
#include "InfoBoxes/InfoBoxManager.hpp"
#include "InfoBoxes/Data.hpp"
#include "Interface.hpp"
#include "Components.hpp"
#include "Engine/GlideSolvers/GlideResult.hpp"
#include "Engine/Task/Solvers/TaskSolution.hpp"
#include "Task/ProtectedTaskManager.hpp"
#include "Engine/Util/Gradient.hpp"
#include "Engine/Task/TaskManager.hpp"
#include "Engine/Task/Unordered/AlternateList.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Dialogs/Task/TaskDialogs.hpp"
#include "Language/Language.hpp"
#include "BackendComponents.hpp"
#include "DataComponents.hpp"

#include <array>
#include <mutex>

namespace {

struct AlternateSlotState {
  AlternateInfoBoxMode mode = AlternateInfoBoxMode::AUTO;
  WaypointPtr waypoint;
};

struct ResolvedAlternateInfo {
  AlternateInfoBoxMode mode = AlternateInfoBoxMode::AUTO;
  WaypointPtr waypoint;
  GlideResult solution{};

  ResolvedAlternateInfo() noexcept {
    solution.Reset();
  }

  [[gnu::pure]]
  bool IsValid() const noexcept {
    return waypoint != nullptr && solution.IsDefined();
  }
};

std::mutex alternate_state_mutex;
/**
 * Runtime-only manual/auto state for the two logical Alternate
 * InfoBox slots.  The three InfoBox variants for each slot share the
 * same entry.
 */
std::array<AlternateSlotState, alternate_info_box_slot_count> alternate_slot_states;

[[gnu::pure]]
const char *
GetAlternateModeShortLabel(AlternateInfoBoxMode mode) noexcept
{
  return mode == AlternateInfoBoxMode::MANUAL
    ? _("MAN")
    : _("AUTO");
}

void
MarkAlternateInfoBoxesDirty() noexcept
{
  InfoBoxManager::SetDirty();
  InfoBoxManager::ScheduleRedraw();
}

AlternateInfoBoxMode
GetAlternateInfoBoxModeUnlocked(AlternateInfoBoxSlot slot) noexcept
{
  return alternate_slot_states[ToAlternateInfoBoxSlotIndex(slot)].mode;
}

WaypointPtr
GetManualAlternateWaypointUnlocked(AlternateInfoBoxSlot slot) noexcept
{
  return alternate_slot_states[ToAlternateInfoBoxSlotIndex(slot)].waypoint;
}

[[gnu::pure]]
GlideResult
SolveManualAlternate(const Waypoint &waypoint) noexcept
{
  const auto &basic = CommonInterface::Basic();
  const auto &calculated = CommonInterface::Calculated();
  const auto &settings = CommonInterface::GetComputerSettings();
  GlideResult solution;
  solution.Reset();

  if (!basic.location_available || !basic.NavAltitudeAvailable())
    return solution;

  if (!calculated.glide_polar_safety.IsValid())
    return solution;

  return TaskSolution::GlideSolutionRemaining(
    basic.location, waypoint.location,
    (waypoint.has_elevation ? waypoint.elevation : 0.) +
      settings.task.safety_height_arrival,
    basic.nav_altitude, calculated.GetWindOrZero(),
    settings.task.glide, calculated.glide_polar_safety);
}

ResolvedAlternateInfo
ResolveAlternateInfo(AlternateInfoBoxSlot slot) noexcept
{
  ResolvedAlternateInfo resolved;
  resolved.mode = GetAlternateInfoBoxMode(slot);

  if (!backend_components || !backend_components->protected_task_manager)
    return resolved;

  if (resolved.mode == AlternateInfoBoxMode::MANUAL) {
    resolved.waypoint = GetManualAlternateWaypoint(slot);
    if (resolved.waypoint != nullptr)
      resolved.solution = SolveManualAlternate(*resolved.waypoint);

    return resolved;
  }

  ProtectedTaskManager::Lease lease{*backend_components->protected_task_manager};
  const AlternateList &alternates = lease->GetAlternates();
  if (alternates.empty())
    return resolved;

  unsigned index = ToAlternateInfoBoxSlotIndex(slot);
  if (index >= alternates.size())
    index = alternates.size() - 1;

  resolved.waypoint = alternates[index].waypoint;
  resolved.solution = alternates[index].solution;
  return resolved;
}

void
SetAlternateTitle(InfoBoxData &data, AlternateInfoBoxSlot slot,
                  const char *suffix) noexcept
{
  const char *mode_label =
    GetAlternateModeShortLabel(GetAlternateInfoBoxMode(slot));
  const unsigned display_number = GetAlternateInfoBoxSlotDisplayNumber(slot);

  if (suffix == nullptr)
    data.FmtTitle(_("Altn {} {}"), display_number, mode_label);
  else
    data.FmtTitle(_("Altn {} {} {}"), display_number, suffix, mode_label);
}

} // namespace

AlternateInfoBoxMode
GetAlternateInfoBoxMode(AlternateInfoBoxSlot slot) noexcept
{
  const std::scoped_lock lock(alternate_state_mutex);
  return GetAlternateInfoBoxModeUnlocked(slot);
}

void
SetAlternateInfoBoxMode(AlternateInfoBoxSlot slot,
                        AlternateInfoBoxMode mode) noexcept
{
  {
    const std::scoped_lock lock(alternate_state_mutex);
    auto &state = alternate_slot_states[ToAlternateInfoBoxSlotIndex(slot)];
    state.mode = mode;
    if (mode == AlternateInfoBoxMode::AUTO)
      state.waypoint.reset();
  }

  MarkAlternateInfoBoxesDirty();
}

WaypointPtr
GetManualAlternateWaypoint(AlternateInfoBoxSlot slot) noexcept
{
  const std::scoped_lock lock(alternate_state_mutex);
  return GetManualAlternateWaypointUnlocked(slot);
}

void
SetManualAlternateWaypoint(AlternateInfoBoxSlot slot,
                           WaypointPtr waypoint) noexcept
{
  {
    const std::scoped_lock lock(alternate_state_mutex);
    alternate_slot_states[ToAlternateInfoBoxSlotIndex(slot)].waypoint =
      std::move(waypoint);
  }

  MarkAlternateInfoBoxesDirty();
}

void
ClearManualAlternateWaypoint(AlternateInfoBoxSlot slot) noexcept
{
  SetManualAlternateWaypoint(slot, nullptr);
}

bool
InfoBoxContentAlternateBase::HandleClick() noexcept
{
  if (!backend_components ||
      !data_components || !data_components->waypoints)
    return false;

  dlgAlternatesListShowModal(data_components->waypoints.get(), slot);
  return true;
}

void
InfoBoxContentAlternateName::Update(InfoBoxData &data) noexcept
{
  SetAlternateTitle(data, slot, nullptr);

  const auto alternate = ResolveAlternateInfo(slot);
  if (!alternate.IsValid() || !CommonInterface::Basic().track_available) {
    data.SetInvalid();
    return;
  }

  data.SetComment(alternate.waypoint->name.c_str());

  // Set Value
  Angle Value = alternate.solution.vector.bearing -
    CommonInterface::Basic().track;

  data.SetValueFromBearingDifference(Value);

  // Set Color (blue/black)
  data.SetValueColor(alternate.solution.IsFinalGlide() ? 2 : 0);
}

void
InfoBoxContentAlternateGR::Update(InfoBoxData &data) noexcept
{
  SetAlternateTitle(data, slot, "GR");

  const auto alternate = ResolveAlternateInfo(slot);
  if (!alternate.IsValid()) {
    data.SetInvalid();
    return;
  }

  data.SetComment(alternate.waypoint->name.c_str());

  double gradient =
    ::AngleToGradient(alternate.solution.DestinationAngleGround());

  if (gradient < 0) {
    data.SetValueColor(0);
    data.SetValue("+++");
    return;
  }
  if (::GradientValid(gradient)) {
    data.SetValueFromGlideRatio(gradient);
  } else {
    data.SetInvalid();
  }

  // Set Color (blue/black)
  data.SetValueColor(alternate.solution.IsFinalGlide() ? 2 : 0);
}

void
InfoBoxContentAlternateAltDiff::Update(InfoBoxData &data) noexcept
{
  SetAlternateTitle(data, slot, "AltD");

  const auto alternate = ResolveAlternateInfo(slot);
  if (!alternate.IsValid()) {
    data.SetInvalid();
    return;
  }

  data.SetComment(alternate.waypoint->name.c_str());

  const auto &settings = CommonInterface::GetComputerSettings();
  auto altitude_difference =
    alternate.solution.SelectAltitudeDifference(settings.task.glide);
  data.SetValueFromArrival(altitude_difference);
}
