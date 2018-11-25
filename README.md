# OMX image viewer

A GPU accelerated image viewer for the Raspberry Pi. 

## Building

You will need the development header for libjpeg and libpng
(Debian: libjpeg8-dev, libpng12-dev). Then build it on the Pi with:

    make ilclient
    make

And install with:

    sudo make install

## Synopsis

USAGE: 
    
    omxiv [OPTIONS] image1 [image2] ...
    omxiv [OPTIONS] directory

    Without any input it will cycle through all
    supported images in the current directory.

OPTIONS:

    -h  --help                   Print this help
    -v  --version                Show version info
    -t                  n        Time in s between 2 images in a slide show
    -b  --blank                  Set background to black
    -T  --transition   type      type: none(default), blend
        --duration      n        Transition duration in ms
        --win     'x1 y1 x2 y2'  Position of image window
        --win      x1,y1,x2,y2   Position of image window
    -m  --mirror                 Mirror image
    -a  --aspect       type      type: letterbox(default), fill, center
    -o  --orientation   n        Orientation of the image (0, 90, 180, 270)
    -l  --layer         n        Render layer number
    -d  --display       n        Display number
    -i  --info                   Print some additional infos
    -k  --no-keys                Disable keyboard input
    -s  --soft                   Force software decoding
        --ignore-exif            Ignore exif orientation

KEY CONFIGURATION:

    ESC, q  :   Quit
    LEFT    :   Previous image
    RIGHT   :   Next image
    UP      :   Rotate right
    DOWN    :   Rotate left
    m       :   Mirror image
    p       :   Pause slide show

## Supported images

* **JPEGs**
  - non-progressive: OMX.broadcom.image_decode
  - progressive: libjpeg
  
* **PNGs**
  - libpng

* **BMPs**
  - libnsbmp
  
* **GIFs**
  - libnsgif
  
* **TIFFs**
  - libtiff

## Credits
**Thanks to:**
  * Matt Ownby, Anthong Sale for their hello_jpeg example
  * Jan Newmarch for his [blog](http://jan.newmarch.name/RPi/index.html): Programming AudioVideo on the Raspberry Pi GPU
  * Various authors of example code and other parts (marked in the source files)

