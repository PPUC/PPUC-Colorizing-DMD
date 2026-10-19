# Sections

The asset lists on the left (`Comparison Masks`, `Backgrounds`, `Sprites`) and the inspector on
the right (`Frame`, `Comparison Mask`, `Background`, `Sprite`, `Dynamic Sets`, `Palette`,
`Color Rotations`, `Color Sets`) are sections one below the other, each with a header that opens
and closes it (a click on the header). Any number can be open at a time: the open sections share
the height of their column (one alone takes all of it, two about half each at first), each
scrolling its own content where it does not fit, and the headers of the closed ones stay
visible. Below an open section with another open section further down there is a thin handle:
dragging it moves the border between the two (the others keep their size, and a section stays
at least 40 pixels high); a double click on a handle gives all open sections the same share
again. Which sections are open, and their shares, are kept for the next start; at first
`Comparison Masks` and `Frame` are open.

Both columns can be made wider or narrower by dragging their edge next to the canvas; the widths
are kept for the next start.
