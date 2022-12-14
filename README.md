# pdvzip
PNG Data Vehicle for Twitter, ZIP Edition (PDVZIP v1.2).

Embed a ZIP file of up to ≈5MB within a tweetable and '*executable*' PNG polyglot image.

![Demo Image](https://github.com/CleasbyCode/pdvzip/blob/main/Demo_Image/demo_image.png)  
{***Image demo: Download and*** [**run me!**](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files) ***Cool A.I images found on Twitter***, [**original thread link**](https://twitter.com/CryptoTea_/status/1611017391709765633)}  

Based on a similar idea from the original Python program ['***tweetable-polyglot-png***'](https://github.com/DavidBuchanan314/tweetable-polyglot-png) created by David Buchanan, pdvzip uses different methods for storing and accessing arbitrary data within a PNG image file.

Compile and run the program under Windows or **Linux**.

## Usage

```c
$ g++ pdvzip.cpp -o pdvzip
$
$ ./pdvzip

Usage:  pdvzip  <png_image>  <zip_file>
        pdvzip  --info

$ ./pdvzip snow_pic.png training_doc.pdf.zip

Created output file 'pdv_training_doc.pdf.zip.png' 3414402 bytes.

All Done!

$

```

Once the ZIP file has been embedded within a PNG image, it's ready to be shared (tweeted) or '*executed*' whenever you want to open/play the media file.

You can also upload and share your PNG image to *some popular image hosting sites, such as [***Flickr***](https://www.flickr.com/), [***ImgBB***](https://imgbb.com/), [***Imgur***](https://imgur.com/a/zF40QMX), [***ImgPile***](https://imgpile.com/), [***ImageShack***](https://imageshack.com/), [***PostImage***](https://postimg.cc/xcCcvpLJ), etc. **Not all image hosting sites are compatible, e.g. [***ImgBox***](https://imgbox.com/), [***Reddit***](https://www.reddit.com/).*

**Mobile Issue**: Sometimes when saving images from Twitter to a mobile, the file gets saved with a '*.jpg*' extension. Please note, the file has not been converted to a JPG. Twitter has just renamed the extension, so it is still the original PNG image with its embedded content. 

## Extracting Your Embedded File(s)
**Linux**    
Make sure image file has executable permissions.
```c

$ chmod +x pdv_your_image_file.png
$
$ ./pdv_your_image_file.png 

```  
**Windows**   
First, rename the '*.png*' file extension to '*.cmd*'.
```c

G:\demo> ren pdv_your_image_file.png pdv_your_image_file.cmd
G:\demo>
G:\demo> .\pdv_your_image_file.cmd

```
Opening the cmd file from the desktop, on its first run, Windows may display a security warning. Clear this by clicking '***More info***' then select '***Run anyway***'.

For some common video & audio files, Linux requires the '***vlc (VideoLAN)***' application, Windows uses the set default media player.  
PDF '*.pdf*', Linux requires the '***evince***' application, Windows uses the set default PDF viewer.  
[Python](https://asciinema.org/a/544680) '*.py*', Linux & Windows use the '***python3***' command to run these programs.  
PowerShell '*.ps1*', Linux uses the '***pwsh***' command (if PowerShell installed), Windows uses '***powershell***' to run these scripts.

For any other file type, Linux & Windows will rely on the operating system's set default application.  
Obviously, the embedded file needs to be compatible with the operating system you run it on.

If the embedded media type is Python, PowerShell, Shell script or a Windows/Linux executable, you can provide optional command-line arguments for your file.

Here is a video example of using **pdvzip** with a simple Shell script (.sh) with arguments for the script file, that are also embedded within the PNG image along with the script file.

[![asciicast](https://asciinema.org/a/542549.svg)](https://asciinema.org/a/542549)
  
To just get access to the file(s) within the ZIP archive, rename the '*.png*' file extension to '*.zip*'. Treat the ZIP archive as read-only, do not add or remove files from the PNG-ZIP polyglot file.

**Image Examples**  

[Twitter. Four images. Embedded with Python, PDF, MP4 & MP3](https://twitter.com/CleasbyCode/status/1612386245266604032)

**Video Examples (YouTube)**

[Embedding, Tweeting & Extracting Multiple Files](https://www.youtube.com/watch_popup?v=VTYP3OdEzRM)

[Embedding & Running Python Program](https://www.youtube.com/watch_popup?v=Ns64VF0dciw)



## PNG Image Requirements for Arbitrary Data Preservation

Note: As I'm still testing the following, information presented here may change frequently. 

PNG file size (image + embedded content) must not exceed **5MB** (5,242,880 bytes).  
Twitter will convert image to ***jpg*** if you exceed this size.

**Dimensions:**

The following dimension size limits are specific to **pdvzip** and not necessarily the extact Twitter size limits.

**PNG_32** (Truecolour with alpha [6])  
**PNG_24** (Truecolour [2]) 

Image dimensions can be set between a minimum of ***68 x 68*** and a maximum of ***899 x 899***.
    
**PNG_8 (Indexed-colour [3])**

Image dimensions can be set between a minimum of ***68 x 68*** and a maximum of ***4096 x 4096***.
        
**Chunks:**  

PNG chunks that you can insert arbitrary data, in which Twitter will preserve in conjuction with the above dimensions & file size limits.  

***bKGD, cHRM, gAMA, hIST,***  
***IDAT,*** (Use as last IDAT chunk, after the final image IDAT chunk).  
***PLTE,*** (Use only with PNG_32 & PNG_24).  
***pHYs, sBIT, sPLT, sRGB,***  
***tRNS.*** (Not recommended as it will distort image).  

This program uses hIST & IDAT chunks for storing arbitrary data and removes the others, apart from iCCP, if found.

## ZIP File Size & Other Important Information

To work out the maximum ZIP file size, start with Twitter's size limit of 5MB (5,242,880 bytes),
minus your PNG image size, minus 750 bytes (internal shell extraction script size).  
  
Example: 5,242,880 - (307,200 + 750) = 4,934,930 bytes available for your ZIP file.  

The less detailed your image, the more space available for the ZIP.

* Make sure your ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.  
* Use file extensions for your media file within the ZIP archive: *my_doc.pdf*, *my_video.mp4*, *my_program.py*, etc.
  
  A file without an extension will be treated as a Linux executable.      
* **Paint.net** application is recommended for easily creating compatible PNG image files.

##
