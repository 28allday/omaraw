# Print and contact sheets

Both make PDFs in the destination folder, from the same Output page.

## Contact sheet

The source photos as a captioned grid: columns × rows, paper, orientation, an optional title. Built from the Library thumbnails, edits included.

## Print layout

Every photo is rendered at 300 dpi for the chosen paper, then placed one per page inside 10 mm margins, the page turning to suit each photo.

- **Paper profile**: an RGB ICC with an intent and black point compensation converts the PDF pages into the paper's colour. When printing that PDF, avoid applying the same conversion again. Without a paper profile, pages stay sRGB.
- **Print PDF, one per page**: writes the finished layout to the destination folder. Open the PDF in another application to print it, or send it to your lab.

## Soft proof

In Develop's left dock: pick the printer's or lab's profile and the viewer shows the render as it would print, with an optional magenta warning for colours the paper cannot hold.
