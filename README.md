# pdvzip
PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP).

Embed a ZIP file of up to ≈5MB within a tweetable and 'executable' PNG polyglot image.

Based on a similar idea from the original Python program ['tweetable-polyglot-png'](https://github.com/DavidBuchanan314/tweetable-polyglot-png) created by David Buchanan, pdvzip uses different methods for storing and accessing arbitrary data within a PNG image file.

Compile and run the program under Windows or **Linux**.

## Usage

```c
$ g++ pdvzip.cpp -o pdvzip
$
$ ./pdvzip

Usage:  pdvzip  <png_image>  <zip_file>
        pdvzip  --info

```

Once the ZIP file has been embedded within a PNG image, it's ready to be shared (tweeted) or 'executed' whenever you want to open/play the media file.

You can also upload your PNG image to *some popular image hosting sites, such as [**Imgur**](https://imgur.com) and [**Postimages**](https://postimages.org), etc.  
*Not all image hosting sites are compatible.

## Accessing Arbitrary Data
**Linux**    
Make sure image file has executable permissions.
```c

$ chmod +x pdv_your_image_file.png
$ ./pdv_your_image_file.png 

```  
**Windows**   
First, rename the '.png' file extension to '.cmd'.
```c

G:\demo> ren pdv_your_image_file.png pdv_your_image_file.cmd
G:\demo> .\pdv_your_image_file.cmd

```
Opening the cmd file from the desktop, on its first run, Windows may display a security warning. Clear this by clicking **'More info'** then select **'Run anyway'**.

For some common video & audio files, Linux requires the 'vlc (VideoLAN)' application, Windows uses the set default media player.  
PDF '.pdf', Linux requires the 'evince' application, Windows uses the set default PDF viewer.  
Python '.py', Linux & Windows use the 'python3' command to run these programs.  
PowerShell '.ps1', Linux uses the 'pwsh' command (if PowerShell installed), Windows uses 'powershell' to run these scripts.

For any other media type/file extension, Linux & Windows will rely on the operating system's set default application. To just get access to the file(s) within the ZIP archive, rename the '.png' file extension to '.zip'. Treat the ZIP archive as read-only, do not add or remove files from the PNG-ZIP polyglot file.

**Video Examples (YouTube)**
1. [Embed, Play & Tweet MP4 Video (Linux & Windows Demo)](https://youtu.be/BwfFDwTSOK8) 
2. [Embed, Tweet & Run Python Program (Linux & Windows Demo)](https://youtu.be/ZubGU_Eb7Ks)
3. [Embed, Open & Tweet PDF Document (Windows & Linux Demo)](https://youtu.be/FnxD9XEjXos)  

**Image Example**  


  Imgur (https://imgur.com/a/zF40QMX). This image contains an embedded PDF document.

## PNG Image Requirements for Arbitrary Data Preservation

Bit depth 8-bit or lower (4,2,1) Indexed colour (PNG colour type value 3).  

Image's multiplied dimensions value must be between 5,242,880 and 5,500,000.
Suggested Width x Height Dimensions: 2900 x 1808 = 5,243,200. Example Two: 2290 x 2290 = 5,244,100, etc.

Valid PNG chunk types that Twitter will preserve arbitrary data: ***bKGD, cHRM, gAMA, hIST, iCCP, pHYs, sBIT, sPLT, sRGB, tRNS***. We can also use an ***IDAT*** chunk type after the last image data IDAT chunk.  This program uses **hIST** & **IDAT** chunk types and removes the others.

* Dimensions provide the storage capacity for our PNG image + arbitrary data. For example, 2900 x 1808 is 5,243,200, slightly over 5MB. This covers us for up to Twitter's 5MB size limit, same with other similar range dimension combinations (e.g. 2290 x 2290). Its pointless going too far over 5,242,880 for W x H dimensions, considering the 5MB PNG size limit.

* Bit depth setting of 8-bit or lower (4,2,1) Indexed colour (PNG colour type value 3), '*enables*' preservation of arbitrary data in the above 11 PNG chunk types.

These settings are mostly the result of trial and error tinkering with PNG image files. As we can't see how the Twitter code is handling image configurations, we are unable to say why this works the way it does.

## ZIP File Size & Other Important Information

To work out the maximum ZIP file size, start with Twitter's size limit of 5MB (5,242,880 bytes),
minus your PNG image size, minus 400 bytes (internal script size).  
Example: 5,242,880 - (307,200 + 400) = 4,935,280 bytes available 
for your ZIP file.  

The less detailed your image, the more space available for the ZIP.

* Make sure your ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.  
* Always use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.  
* Paint.net application is recommended for easily creating compatible PNG image files.

**Video Example**

[Paint.net: Configure Correct PNG Image Settings for PDVZIP](https://youtu.be/nMlUNdiaS88)

##
