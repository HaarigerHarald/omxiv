# OMX image viewer

A GPU accelerated image viewer for the Raspberry Pi. 

## Building

You will need the development header for libjpeg, libpng and optional 
libcurl (Debian: libjpeg8-dev, libpng12-dev, libcurl4-openssl-dev). 
Then build it on the Pi with:

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
  
* ##### GIFs
  - libnsgif

## Credits
##### Thanks to:
  * Matt Ownby, Anthong Sale for their hello_jpeg example
  * Jan Newmarch for his [blog](http://jan.newmarch.name/RPi/index.html): Programming AudioVideo on the Raspberry Pi GPU
  * Various authors of example code and other parts (marked in the source files)

