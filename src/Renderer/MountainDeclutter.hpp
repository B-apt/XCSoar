// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "Engine/Waypoint/Waypoint.hpp"

/**
 * Minimum on-screen distance (pixels, before #Layout::Scale()) between
 * two decluttered mountain waypoints.  Converted to metres with the
 * current map scale, it becomes the separation a waypoint's
 * #Waypoint::isolation must reach to be drawn.
 *
 * Because it derives from the map scale alone and never from the map
 * centre, the selection does not change while the user pans.
 */
inline constexpr unsigned MOUNTAIN_DECLUTTER_SEPARATION_PX = 56;

/**
 * Should this waypoint be hidden by the mountain declutter?
 *
 * @param min_separation the separation in metres that #Waypoint::isolation
 * must reach at the current map scale
 */
[[gnu::pure]]
inline bool
IsMountainDecluttered(const Waypoint &wp, double min_separation) noexcept
{
  switch (wp.type) {
  case Waypoint::Type::MOUNTAIN_TOP:
  case Waypoint::Type::MOUNTAIN_PASS:
    /* a negative isolation means it was never computed; such a waypoint
       stays visible */
    return wp.isolation >= 0 && wp.isolation < min_separation;

  default:
    return false;
  }
}
