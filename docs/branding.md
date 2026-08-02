# Application icon assets

`data/icons/app-icon/CodecDeck-app-icon.svg` is the source application icon.
The PNG exports under `data/icons/hicolor` are generated from that vector and
installed at the standard desktop icon sizes from 16 px through 1024 px.

The 512 px export is also embedded in the executable and used as the runtime
window icon. When updating the artwork, regenerate each raster size directly
from the SVG instead of resizing an existing PNG.
