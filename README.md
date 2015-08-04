# OMX image viewer

A GPU accelerated image viewer for the Raspberry Pi. 

## Building

Make sure that you have the development header for libjpeg, libpng and optional 
libcurl installed (libjpeg8-dev, libpng12-dev, libcurl4-openssl-dev), then build 
it on the Pi with:

    make ilclient
    make

Or if you want to build without libcurl:

    make ilclient
    make CURL=0

And install with:

    sudo make install

## Supported images
* ##### JPEGs
  - non-progressive: OMX image_decode
  - progressive: libjpeg
  
* ##### PNGs
  - libpng

* ##### BMPs
  - libnsbmp

## Credits
##### Thanks to:
  * Matt Ownby, Anthong Sale for their hello_jpeg example
  * Jan Newmarch for his [blog](http://jan.newmarch.name/RPi/index.html): Programming AudioVideo on the Raspberry Pi GPU
  * The xbmc/kodi guys for their omxplayer
  * Everyone else I might have forgotten

