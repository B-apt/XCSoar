// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapWindow.hpp"
#include "InfoBoxes/Content/Alternate.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "Screen/Layout.hpp"
#include "Look/MapLook.hpp"
#include "ui/canvas/Pen.hpp"
#include "ui/canvas/Color.hpp"

void
MapWindow::DrawAlternateLines(Canvas &canvas, PixelPoint aircraft_pos) noexcept
{
  // Vérifier si l'option est activée
  if (!GetComputerSettings().task.draw_lines_to_alternates)
    return;

  // Vérifier si la position de l'avion est disponible
  const NMEAInfo &basic = Basic();
  if (!basic.location_available)
    return;

  // Créer un stylo bleu en pointillés
  Pen alternate_pen;
  static constexpr Color clrBlupia(0x38,0x55,0xa7);
  alternate_pen.Create(Pen::DASH1, Layout::ScalePenWidth(2), clrBlupia);
  canvas.Select(alternate_pen);

  // Accéder aux alternates des 2 slots (FIRST et SECOND)
  const std::lock_guard lock{alternate_state_mutex};

  // Dessiner une ligne vers chaque alternate valide
  for (unsigned i = 0; i < alternate_info_box_slot_count; ++i) {
    const auto slot = static_cast<AlternateInfoBoxSlot>(i);
    const WaypointPtr wp = GetAlternateSlotWaypointUnlocked(slot);

    if (wp != nullptr && wp->location.IsValid()) {
      // Convertir la position du waypoint en coordonnées écran
      // GeoToScreen retourne toujours une position (même hors écran)
      const PixelPoint wp_pos = render_projection.GeoToScreen(wp->location);
      canvas.DrawLine(aircraft_pos, wp_pos);
    }
  }
}
