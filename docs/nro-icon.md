# NRO icon

Reconstructed with the built-in ImageGen tool from the user's supplied Moonlight
reference, replacing the earlier crescent concept. The mark is a white circle
split into eight sectors on a solid gray background.

- High-resolution generated master: `resources/img/moonlight_icon-flat.png`.
- NRO icon: `resources/img/moonlight_icon.jpg` (256x256, RGB baseline JPEG).
- The JPEG is resized from the master with high-quality bicubic sampling and
  saved at quality 95 for the Switch launcher. `PROJECT_ICON` already points
  to this file; the streaming implementation is unaffected.

## ImageGen prompt

Recreate and clean up the supplied Moonlight logo icon for a Nintendo Switch NRO launcher icon. Preserve the EXACT logo identity from this reference: a perfectly round white disc split into eight equal 45-degree white sectors by four narrow straight gray diameters, horizontal, vertical, and two diagonals. All separator gaps have equal constant width and intersect exactly at center. These are solid white pie sectors, NOT a crescent moon, NOT an aperture spiral, NOT blades, NOT a sun with external rays. Match the reference proportions: the outer white circle occupies about 79% of the square's width, is perfectly centered, with equal generous margins; gray separators about 3% of square width. Background and separator gaps are one perfectly uniform solid neutral slate gray matching the reference approximately #575E65. Pure white #FFFFFF flat sectors. Produce clean, precise, symmetrical, sharp vector-like edges with high-quality antialiasing, optimized to remain crisp after downsampling to 256x256. Square artwork edge to edge, no rounded outer corners, no surrounding frame, no text, no watermark. Absolutely no gradients, shading, texture, lighting, shadows, glow or 3D. This is a faithful high-resolution reconstruction of the provided mark, not a redesign.
