# Lens & composition

The **Lens & Geometry** section holds lens correction, transform, defringe, chromatic aberration and orientation. The Crop toolbar has **Precise crop edges** for numeric boundaries.

## Lens profile

At the top: what the lens database matched for the camera body and the lens the file names, each marked when it is not in the database. Type part of the maker or lens name to filter the lenses listed for the body's mount, for example **Canon 28**. Typing only searches. Click a result to apply it, or press Enter to choose the first match. Clear the search or press Escape to cancel it. The arrow beside the heading goes back to the file's own lens.

## Lens corrections

- **Correction data**: Camera data uses what the maker wrote into the file; Lens database uses the matched profile.
- **Corrections**: which of distortion, vignetting and chromatic aberration to apply.
- **Scale**: zooms in to hide the edges that distortion correction pulls into view.

## Orientation

Set the whole picture's orientation. Each choice appears once: **As shot**,
**Unrotated**, **Rotate 90° left**, **Rotate 90° right**, **Rotate 180°**,
**Flip horizontally**, **Flip vertically**, and the two combined rotate-and-flip
choices.

**As shot** follows the camera's orientation tag. **Unrotated** uses the file's
original pixel order. The other choices also start from that original order;
choosing a rotation again does not add another turn. The combined choices rotate
90° right, then flip in the displayed direction. For small angle corrections,
use **Crop & straighten**.

## Defringe

Removes purple and green fringing at high-contrast edges. **Method**, **Radius** and **Threshold** control how it finds the fringes.

The separate **Chromatic aberration** tool works on Bayer RAW sensor data only.
It has no effect on X-Trans RAWs, linear DNGs or JPEG/PNG/TIFF images. For those
images, use **Lens corrections** with a matching profile, or **Defringe**.

## Transform

- **Vertical perspective** and **Horizontal perspective**: manual correction for converging lines, such as buildings photographed from below.

**Crop & straighten** opens the single set of rotation, automatic/guided correction and crop-edge controls. **Straighten** and **Crop edges** live there, rather than being repeated in Transform.

## Crop

**Left**, **Top**, **Right**, **Bottom** as fractions of the frame. The crop tool (R) is the easier way: drag the handles or edges, pick a ratio, straighten with the slider or draw a line along something that should be level. Guides: thirds, golden, diagonals or none. Escape closes the tool.

With a ratio locked, dragging an edge keeps the crop centred on the other
axis. A corner responds to horizontal and vertical movement, and the ratio
stays locked at the photo boundaries. Each completed drag is one undo step;
closing the tool during a drag cancels that unfinished movement.
**Original** follows the photo's selected orientation. Choosing a ratio or
swapping landscape and portrait fits the frame in one undoable step.

## Automatic geometry and guides

Open **Crop and straighten** (R):

- **Auto level** finds clear horizontal and vertical edges and levels the photo.
- **Auto perspective** offers Vertical, Horizontal and Full. Use Vertical for leaning buildings; Full needs clear lines in both directions.
- **Guided** lets you draw two edges that should be vertical or two that should be horizontal. Draw four guides to correct both directions, then choose **Apply guides**. Clear guides starts again; Cancel guides leaves the photo unchanged.
- **Crop edges** trims the empty corners produced by rotation and perspective. Largest keeps the largest usable area; Original keeps the original aspect ratio. Off shows the full transformed frame. The policy also follows manual rotation and perspective changes.
- **Reset geometry** resets rotation and perspective while preserving the crop frame and other adjustments. **Reset crop frame** restores the whole frame and Free aspect ratio while keeping geometry.

Corrections appear in the preview and each is one undoable history step. An image without enough reliable lines stays unchanged; use the ruler or draw guides instead. These tools need the updated OmaRAW engine.
