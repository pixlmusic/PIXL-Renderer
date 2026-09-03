PIXL QUALITY PREVIEW IMAGES

Drop your authored preview PNGs into this folder. No rebuild is required.

The release UI uses these exact files:

Profile_LOW.png
Profile_MEDIUM.png
Profile_HIGH.png
Profile_ULTRA.png
Neural_ULTRA.png

Recommended:
- same aspect ratio for every image
- 16:9 or 3:2
- 800-1280 pixels wide is sufficient for the UI
- capture the same scene/camera for all four tiers in a category

The Quality page loads profile images lazily when a main profile is hovered.
Neural_ULTRA is shown while Neural Rendering is hovered on the Camera page.
Individual engineering sliders intentionally use concise text tooltips instead of
separate images. If an image is absent, PIXL displays a clean placeholder.
