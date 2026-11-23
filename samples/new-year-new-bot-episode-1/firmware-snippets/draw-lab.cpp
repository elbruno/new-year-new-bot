// Selected display excerpt from drawLab(uint32_t now).
// The surrounding redraw condition and state handling are omitted.
M5.Display.pushImage(0, 0, 320, 240, labPixels(expression));
faceReplaced = true;
invalidateStatusStrip();
