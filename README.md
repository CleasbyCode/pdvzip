# pdvzip
A simple command-line tool used to embed a ZIP file within a PNG image,  
creating a *tweetable* and "[***executable***](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files)" PNG-ZIP polyglot image file.  

You can share your image on X (Twitter) and a few other hosting sites, which will preserve the embedded **ZIP** file.

**Image size limits vary across platforms:**

* *Flickr (200MB), ImgBB (32MB), PostImage (24MB), ImgPile (8MB), Twitter (5MB).*
  
![Demo Image](https://github.com/CleasbyCode/pdvzip/blob/main/demo_image/pdvimg_30657.png)  
***{Image credit: [MΞV.ai / @aest_artificial](https://twitter.com/aest_artificial)}*** 
 
Based on the similar idea by [***David Buchanan***](https://www.da.vidbuchanan.co.uk/), from his original Python program [***tweetable-polyglot-png***](https://github.com/DavidBuchanan314/tweetable-polyglot-png),  
**pdvzip** uses different methods for [***storing***](https://github.com/CleasbyCode/pdvzip#png-image-requirements-for-arbitrary-data-preservation) and [***accessing***](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files) embedded files within a PNG image.  

Demo Videos: [***Twitter***](https://youtu.be/HlAoVHWoOO0) / [***Flickr***](https://youtu.be/xAEoU3C8HRA)

Compile and run the program under Windows or **Linux**.  

## Usage

```console
user1@linuxbox:~/Desktop$ g++ pdvzip.cpp -O2 -s -o pdvzip
user1@linuxbox:~/Desktop$ ./pdvzip

Usage: pdvzip <cover_image> <zip_file>
       pdvzip --info

user1@linuxbox:~/Desktop$ ./pdvzip plate_image.png like_spinning_plates.zip

Reading files. Please wait...

Updating extraction script.

Embedding extraction script within the PNG image.

Embedding ZIP file within the PNG image.

Writing ZIP embedded PNG image out to disk.

Saved PNG image: pzip_55183.png 4038367 Bytes.

Complete!

You can now share your PNG-ZIP polyglot image on the relevant supported platforms.

```
After embedding the ZIP within an image, it can then be posted on a variety of social media/image hosting sites. "*Execute*" the image whenever you want to access the embedded file(s).

## Extracting Your Embedded File(s)  
**Important:** *When ***saving*** images from **Twitter**, ***click the image in the post to fully expand it***, before saving.*

**Linux** ***(Make sure image file has executable permissions)***
```console

user1@linuxbox:~/Desktop$ chmod +x pzip_55183.png
user1@linuxbox:~/Desktop$ ./pzip_55183.png

```  
Alternative execution (Linux).  Using ***wget*** to download & run the image directly from the hosting site.  
**ImgBB** *wget* example: **Image with embedded python script**.
```console

$ wget "https://i.ibb.co/r6R7zdG/Fibonacci.png";chmod +x Fibonacci.png;./Fibonacci.png

```   

**Windows** ***(Rename the image file extension to '.cmd')***
```console

G:\demo> ren pzip_55183.png pzip_55183.cmd
G:\demo> .\pzip_55183.cmd

```
Alternative execution (Windows).  Using ***iwr*** to download & run the image directly from the hosting site.  
**Twitter** *iwr* example: **Image with embedded mp3 music file.**
```console

G:\demo> iwr -o img2.cmd "https://pbs.twimg.com/media/FsPOkPnWYAA0935?format=png";.\img2.cmd

```

Opening the cmd file from the desktop, on its first run, Windows may display a security warning.  
Clear this by clicking '***More info***' then select '***Run anyway***'.  

To avoid security warnings, run the image file from a Windows console, as shown in the above example.  

For common video/audio files, Linux will use ***vlc*** or ***mpv*** media players. Windows uses the default media player.  
PDF '*.pdf*', Linux will use ***evince or ***firefox***. Windows uses the default PDF viewer.  
Python '*.py*', Linux & Windows use ***python3*** to run these programs.  
PowerShell '*.ps1*', Linux uses ***pwsh*** (if installed), Windows uses either ***powershell.exe*** or ***pwsh.exe*** to run these scripts.

For any other file types, Linux & Windows will rely on the operating system's set default application.  
Obviously, the embedded file needs to be compatible with the operating system you run it on.

If the zipped embedded media type is PowerShell, Python, Shell script or a Windows/Linux executable,  
pdvzip will give you the option to provide command-line arguments for your file, if required.  

Make sure to enclosed arguments containing spaces, such as file and directory names, within quotation marks, e.g. : -e ../a_file.jpg "../another file.pdf"  
Also be aware when using arguments, you are always working within the subdirectory ./pdvzip_extracted.

[**Here is a video example**](https://asciinema.org/a/542549) of using **pdvzip** with a simple Bash shell script (.sh) with arguments, that are also embedded within the PNG image along with the extraction script.
  
To just get access to the file(s) within the ZIP archive, rename the '*.png*' file extension to '*.zip*'. Treat the ZIP archive as read-only, do not add or remove files from the PNG-ZIP polyglot file.

## PNG Image Requirements for Arbitrary Data Preservation


PNG file size (image + embedded content) must not exceed the hosting site's size limits.  
The site will either refuse to upload your image or it will convert your image to ***jpg***, such as Twitter.

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
        
**PNG Chunks:**  

With **Twitter**, for example, you can overfill the following PNG chunks with arbitrary data,  
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

***bKGD, cHRM, gAMA, hIST,***  
***iCCP,*** (Limited chunk. Only 10KB max. with Twitter).  
***IDAT,*** (Use as last IDAT chunk, after the final image IDAT chunk).  
***PLTE,*** (Use only with PNG-32/24 images).  
***pHYs, sBIT, sPLT, sRGB,***   
***tRNS. (Not recommended, may distort image).***  

*Other platforms may differ in what chunks they preserve and which ones you can overfill.*
  
pdvzip uses the ***iCCP*** (stores small extraction script) and ***IDAT*** (stores the ZIP file) chunks for your arbitrary data.

## ZIP File Size & Other Important Information

To work out the maximum ZIP file size, start with the site's size limit, minus your PNG image size, minus 2500 bytes (extraction script size).  
  
Twitter example: (5MB limit) 5,242,880 - (307,200 [PNG image size] + 2500 [extraction script size]) = 4,933,100 bytes available for your ZIP file.  

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
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)  

##
