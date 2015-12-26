# OMX image viewer

A GPU accelerated image viewer for the Raspberry Pi. 

## Building

You will need the development header for libjpeg and libpng
(Debian: libjpeg8-dev, libpng12-dev). Then build it on the Pi with:

    make ilclient
    make

And install with:

    sudo make install

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

