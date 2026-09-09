// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Isolation.hpp"
#include "Waypoints.hpp"

#include <algorithm>

namespace {

constexpr bool
IsMountain(const Waypoint &wp) noexcept
{
  return wp.type == Waypoint::Type::MOUNTAIN_TOP ||
    wp.type == Waypoint::Type::MOUNTAIN_PASS;
}

/**
 * Does @p a outrank @p b?  Elevation decides; #Waypoint::id breaks ties,
 * so that of two equally high neighbours exactly one suppresses the
 * other, and it is always the same one.
 */
constexpr bool
IsHigherRanked(const Waypoint &a, const Waypoint &b) noexcept
{
  const auto a_elevation = a.GetElevationOrZero();
  const auto b_elevation = b.GetElevationOrZero();

  if (a_elevation != b_elevation)
    return a_elevation > b_elevation;

  return a.id > b.id;
}

[[gnu::pure]]
double
CalculateIsolation(const Waypoints &waypoints, const Waypoint &wp) noexcept
{
  for (double range = 1000;; range *= 2) {
    range = std::min(range, MOUNTAIN_ISOLATION_MAX);

    /* pick the nearest higher-ranked neighbour of the same type; the
       flat projected distance is enough to choose between candidates,
       and is much cheaper than a great-circle distance */
    const Waypoint *nearest = nullptr;
    unsigned nearest_flat_distance = 0;

    waypoints.VisitWithinRange(wp.location, range,
                               [&](const WaypointPtr &other){
      if (other->type != wp.type || !IsHigherRanked(*other, wp))
        return;

      const unsigned distance = wp.FlatDistanceTo(other->flat_location);
      if (nearest == nullptr || distance < nearest_flat_distance) {
        nearest = other.get();
        nearest_flat_distance = distance;
      }
    });

    if (nearest != nullptr) {
      const auto distance = wp.location.DistanceS(nearest->location);

      /* VisitWithinRange() searches a square box, so a hit may sit
         outside the requested radius; trust it only once it is inside,
         because otherwise a nearer one may still hide in the next ring */
      if (distance <= range)
        return distance;
    }

    if (range >= MOUNTAIN_ISOLATION_MAX)
      return MOUNTAIN_ISOLATION_MAX;
  }
}

} // namespace

void
CalculateMountainIsolation(Waypoints &waypoints) noexcept
{
  for (const auto &i : waypoints) {
    if (!IsMountain(*i))
      continue;

    // TODO: eliminate this const_cast hack
    Waypoint &wp = const_cast<Waypoint &>(*i);
    wp.isolation = CalculateIsolation(waypoints, wp);
  }
}
