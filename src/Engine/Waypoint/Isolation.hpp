// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

class Waypoints;

/**
 * Upper bound (m) for #Waypoint::isolation.  Anything at least this far
 * from a higher neighbour is stored as fully isolated, which keeps the
 * search cost bounded for the handful of waypoints that dominate a whole
 * mountain range.
 *
 * The map never asks for a larger separation than this: mountains stop
 * being drawn above #GetMapScale() 10000 anyway (see
 * #WaypointDrawMaxScale()), which caps the requested separation at a few
 * tens of kilometres.
 */
inline constexpr double MOUNTAIN_ISOLATION_MAX = 100000;

/**
 * Fill #Waypoint::isolation for every #Waypoint::Type::MOUNTAIN_TOP and
 * #Waypoint::Type::MOUNTAIN_PASS, leaving all other types untouched.
 *
 * The two types are ranked in separate pools, so a low pass never
 * suppresses a high summit and vice versa.
 *
 * Waypoints::Optimise() must have been called first, because this needs
 * #Waypoint::flat_location.
 */
void
CalculateMountainIsolation(Waypoints &waypoints) noexcept;
