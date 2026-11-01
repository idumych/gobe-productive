// The zoom's slider (TScaleSlider::SetValue, SetScale): a position from 0
// to 1 stands for a zoom from 1/64 to 64 on a logarithmic scale, the
// Scale floater's slider (DIAL 72) showing it in hundredths.
#ifndef GOBE_ZOOM_H
#define GOBE_ZOOM_H

namespace gobe {

// TScaleSlider::SetValue: position `value` (clamped to 0..1) as a zoom:
// z = 2 to the power (value * 12 - 6), rounded (halves up: int(x + 0.5))
// to whole percents when z < 0.15, to 5% when z < 1, to 25% when z <= 3
// and to 100% above that, then clamped to 1/64..64
float ZoomForSliderValue(float value);

// TScaleSlider::SetScale: the position for zoom `zoom` (clamped to
// 1/64..64 first): (6 + log2(zoom)) / 12, clamped to 0..1
float SliderValueForZoom(float zoom);

} // namespace gobe

#endif
