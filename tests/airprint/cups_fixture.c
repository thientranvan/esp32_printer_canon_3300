#include <cups/raster.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
 cups_mode_t mode=atoi(argv[1])?CUPS_RASTER_WRITE_APPLE:CUPS_RASTER_WRITE_PWG;
 cups_raster_t *r=cupsRasterOpen(1,mode);cups_page_header2_t h;memset(&h,0,sizeof(h));
 strcpy(h.MediaClass,"PwgRaster");strcpy(h.cupsPageSizeName,"iso_a4_210x297mm");
 h.HWResolution[0]=h.HWResolution[1]=600;h.cupsWidth=4960;h.cupsHeight=7014;
 h.PageSize[0]=595;h.PageSize[1]=842;h.cupsBitsPerColor=8;h.cupsBitsPerPixel=atoi(argv[2]);h.cupsNumColors=h.cupsBitsPerPixel==24?3:1;
 if(h.cupsBitsPerPixel==1)h.cupsBitsPerColor=1;
 h.cupsBytesPerLine=(h.cupsWidth*h.cupsBitsPerPixel+7)/8;h.cupsColorSpace=h.cupsNumColors==3?CUPS_CSPACE_SRGB:CUPS_CSPACE_SW;
 unsigned pages=argc>3?atoi(argv[3]):1;unsigned duplex=argc>4?atoi(argv[4]):1;
 h.cupsInteger[0]=pages;h.cupsColorOrder=CUPS_ORDER_CHUNKED;h.Duplex=duplex!=1;h.Tumble=duplex==3;
 unsigned char *line=malloc(h.cupsBytesPerLine);
 for(unsigned page=0;page<pages;++page){
 if(!cupsRasterWriteHeader2(r,&h))return 2;
 for(unsigned y=0;y<h.cupsHeight;y++){
  memset(line,255,h.cupsBytesPerLine);
  if(y>=200&&y<240){if(h.cupsBitsPerPixel==1)memset(line+40,0,30);else for(unsigned x=320;x<560;x++)memset(line+x*h.cupsNumColors,0,h.cupsNumColors);}
  if(cupsRasterWritePixels(r,line,h.cupsBytesPerLine)!=h.cupsBytesPerLine)return 3;
 }
 }
 free(line);cupsRasterClose(r);return 0;
}
