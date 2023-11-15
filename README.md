# pdvzip
Command-line tool for embedding a **ZIP** file within a *tweetable* and *"executable"* **PNG** image.  

Share your *zip-embedded* image on the following compatible sites.

* ***Flickr (200MB), ImgBB (32MB), PostImage (24MB), Imgur (20MB / with --imgur option)***
* ***Reddit (20MB), ImgPile (8MB), Twitter & Imgur (5MB).***

![Demo Image](https://github.com/CleasbyCode/pdvzip/blob/main/demo_images/pdvimg_55183.png)  
 ***{Image Credit:*** [***@LyricAIrtist*** ](https://twitter.com/LyricAIrtist/status/1720055981730680859)***}***  
 
Based on a similar idea from the original Python program ["***tweetable-polyglot-png***"](https://github.com/DavidBuchanan314/tweetable-polyglot-png) created by [***David Buchanan***](https://www.da.vidbuchanan.co.uk/),  
pdvzip, written in C++, uses different methods for storing and accessing embedded data within a PNG image file.  

[***Video Demo 1: PNG Image (20MB) Posted to Reddit***](https://youtu.be/c9s1KFQ7wB8)  
[***Video Demo 2: PNG Image (20MB) Posted to Imgur (Using --imgur option)***](https://youtu.be/_QUDNH9OaTM)  

*Using the **--imgur** option with pdvzip, increases the Imgur PNG upload size limit from 5MB to **20MB**.*

*Once the PNG image has been uploaded to your Imgur page, you can grab links of the image for sharing.* 

\**If the embedded image is over **5MB** I would **not recommend** posting the image to the **Imgur Community Page**,  
as the thumbnail preview fails and shows as a broken icon image.  
(Clicking the "broken" preview image will still take you to the correctly displayed full image).*

Compile and run the program under Windows or **Linux**.

## Usage

```console
user1@linuxbox:~/Desktop$ g++ pdvzip.cpp -O2 -s -o pdvzip
user1@linuxbox:~/Desktop$
user1@linuxbox:~/Desktop$ ./pdvzip

Usage: pdvzip <png_image> <zip_file>
       pdvzip <png_image> <zip_file> [--imgur]
       pdvzip --info

user1@linuxbox:~/Desktop$ ./pdvzip plate_image.png like_spinning_plates.zip

Reading files. Please wait...

Updating extraction script.

Embedding extraction script within the PNG image.

Embedding zip file within the PNG image.

Writing zip-embedded PNG image out to disk.

Created zip-embedded PNG image: "pdvimg_55183.png" Size: "4038367 Bytes"

Complete!

You can now share your zip-embedded PNG image on the relevant supported platforms.

```

Once the ZIP file has been embedded within a PNG image, it can be shared on your chosen hosting site or '*executed*' whenever you want to access the embedded file(s).

## Extracting Your Embedded File(s)
**Linux** ***(Make sure image file has executable permissions)***
```console

user1@linuxbox:~/Desktop$ chmod +x pdvimg_55183.png
user1@linuxbox:~/Desktop$
user1@linuxbox:~/Desktop$ ./pdvimg_55183.png

```  
Alternative execution (Linux).  Using ***wget*** to download & run image directly from the hosting site.  
Hosting sites ***wget*** examples:-  
* Twitter (mp3), Flickr (flac) & ImbBB (python).
```console

$ wget "https://pbs.twimg.com/media/FsPOkPnWYAA0935?format=png";mv "FsPOkPnWYAA0935?format=png" pdv_pic.png;chmod +x pdv_pic.png;./pdv_pic.png
$ wget "https://live.staticflickr.com/65535/53012539459_b05ca283d4_o_d.png";mv "53012539459_b05ca283d4_o_d.png" rain.png;chmod +x rain.png;./rain.png
$ wget "https://i.ibb.co/r6R7zdG/Fibonacci.png";chmod +x Fibonacci.png;./Fibonacci.png

```   

**Windows**   
First, rename the file extension to '*.cmd*'.
```console

G:\demo> ren pdvimg_55183.png pdvimg_55183.cmd
G:\demo>
G:\demo> .\pdvimg_55183.cmd

```
Alternative execution (Windows).  Using ***iwr*** to download & run image directly from the hosting site.  
Hosting sites ***iwr*** examples:-  
* Twitter (mp3), Flickr (flac) & ImbBB (python).
```console

G:\demo> iwr -o img2.cmd "https://pbs.twimg.com/media/FsPOkPnWYAA0935?format=png";.\img2.cmd
G:\demo> iwr -o img4.cmd "https://live.staticflickr.com/65535/53012539459_b05ca283d4_o_d.png";.\img4.cmd
G:\demo> iwr -o img5.cmd "https://i.ibb.co/r6R7zdG/Fibonacci.png";.\img5.cmd

```

Opening the cmd file from the desktop, on its first run, Windows may display a security warning.  
Clear this by clicking '***More info***' then select '***Run anyway***'.  

To avoid security warnings, run the image file from a Windows console, as shown in the above example.  

For common video/audio files, Linux requires '***vlc (VideoLAN)***', Windows uses the set default media player.  
PDF '*.pdf*', Linux requires the '***evince***' application, Windows uses the set default PDF viewer.  
Python '*.py*', Linux & Windows use the '***python3***' command to run these programs.  
PowerShell '*.ps1*', Linux uses the '***pwsh***' command, Windows uses '***powershell***' to run these scripts.

For any other file type, Linux & Windows will rely on the operating system's set default application.  
Obviously, the embedded file needs to be compatible with the operating system you run it on.

If the embedded media type is Python, PowerShell, Shell script or a Windows/Linux executable, you can provide optional command-line arguments for your file.

[**Here is a video example**](https://asciinema.org/a/542549) of using **pdvzip** with a simple Shell script (.sh) with arguments for the script file, that are also embedded within the PNG image along with the script file.
  
To just get access to the file(s) within the ZIP archive, rename the '*.png*' file extension to '*.zip*'. Treat the ZIP archive as read-only, do not add or remove files from the PNG-ZIP polyglot file.

## PNG Image Requirements for Arbitrary Data Preservation


PNG file size (image + embedded content) must not exceed the hosting site's size limits.  
The site will either refuse to upload your image or it will convert your image to ***jpg***, such as Twitter & Imgur.

**Dimensions:**

The following dimension size limits are specific to **pdvzip** and not necessarily the extact hosting site's size limits.
These dimension size limits are for compatibility reasons, allowing it to work with all the above listed platforms.

**PNG-32** (Truecolor with alpha [6])  
**PNG-24** (Truecolor [2]) 

Image dimensions can be set between a minimum of ***68 x 68*** and a maximum of ***899 x 899***.

*Note: Images that are created & saved within your image editor as **PNG-32/24** that are either
black & white/grayscale, images with 256 colors or less, will be converted by **Twitter** to
**PNG-8** and you will lose the embedded content. If you want to use a simple "single" color
**PNG-32/24** image, then fill an area with a gradient color instead of a single solid color. 
**Twitter** should then keep the image as **PNG-32/24**. [**(Example).**](https://twitter.com/CleasbyCode/status/1694992647121965554)*
    
**PNG-8 (Indexed-color [3])**

Image dimensions can be set between a minimum of ***68 x 68*** and a maximum of ***4096 x 4096***.
        
**Chunks:**  

With **Twitter**, for example, you can overfill the following PNG chunks with arbitrary data,  
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

***bKGD, cHRM, gAMA, hIST,***  
***iCCP,*** (Only 10KB max. with Twitter).  
***IDAT,*** (Use as last IDAT chunk, after the final image IDAT chunk).  
***PLTE,*** (Use only with PNG-32/24 images).  
***pHYs, sBIT, sPLT, sRGB,*** (Imgur does not keep the pHYs chunk).   
***tRNS. (Not recommended, may distort image).***  

*Other platforms may differ in what chunks they preserve and which ones you can overfill.*
  
This program uses the ***iCCP*** (extraction script) and ***IDAT*** (zip file) chunk names for storing arbitrary data.

## ZIP File Size & Other Important Information

To work out the maximum ZIP file size, start with the hosting site's size limit,  
minus your PNG image size, minus 750 bytes (internal shell extraction script size).  
  
Twitter example: (5MB) 5,242,880 - (307,200 + 750) = 4,934,930 bytes available for your ZIP file.  

The less detailed your image, the more space available for the ZIP.

* Make sure your ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.
* Do not include other .zip files within the main ZIP archive. (.rar files are ok).
* Do not include other pdvzip created PNG image files within the main ZIP archive, as they are essentially .zip files.
* Use file extensions for your file(s) within the ZIP archive: *my_doc.pdf*, *my_video.mp4*, *my_program.py*, etc.
  
  A file without an extension will be treated as a Linux executable.      
* **Paint.net** application is recommended for easily creating compatible PNG image files.  

My other programs you may find useful:-

* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [pdvrdt: CLI tool to encrypt, compress & embed any file type within a PNG image.](https://github.com/CleasbyCode/pdvrdt)
* [xif: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/xif)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)  

##
