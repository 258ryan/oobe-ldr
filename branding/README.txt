Put your own branding pictures here. The folder is copied to C:\vista-oobe\oobe\branding by prepare-vista-oobe.bat
(or copy it there yourself, no rebuild needed).

The wizard asks Windows for its banner (the big "Windows Vista ..." picture on the first page) with
BrandingLoadBitmap(L"Basebrd", 121). To replace it, add one of:

    Basebrd_121.png   (or .bmp / .jpg / .gif)
    banner.png        (same thing, shorter name)

Transparent parts of a PNG become white. Use a picture of the size you want shown (the reference Vista Business banner is
about 412 x 72 with white background). Other pictures use the pattern  <name>_<id>.<ext>  - the log lists every
request as  [branding] BrandingLoadBitmap(<name>, <id>)  so you can see which ids the wizard asks for.
