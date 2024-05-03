# pdvzip
Embed a ZIP/JAR file within a PNG image, to create a *tweetable* and "[*executable*](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files)" PNG-ZIP/JAR polyglot file.  

You can post your image on **X/Twitter** and a few other hosting sites, which retain the embedded file.

**Image size limits:** *X/Twitter (5MB), Flickr (200MB), ImgBB (32MB), PostImage (24MB), ImgPile (8MB).*
  
![Demo Image](https://github.com/CleasbyCode/pdvzip/blob/main/demo_image/pzip_19690.png)  
 
Based on the similar idea by [***David Buchanan***](https://www.da.vidbuchanan.co.uk/), from his original Python program [***tweetable-polyglot-png***](https://github.com/DavidBuchanan314/tweetable-polyglot-png),  
**pdvzip** uses different methods for [***storing***](https://github.com/CleasbyCode/pdvzip#png-image-requirements-for-arbitrary-data-preservation) and [***accessing***](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files) embedded files within a PNG image.  

Demo Videos: [***X/Twitter***](https://youtu.be/HlAoVHWoOO0) | [***Flickr***](https://youtu.be/xAEoU3C8HRA) | [***Jar File*** (Linux/Windows)](https://youtu.be/3FZ8COgG0vU)

Compile and run the program under **Linux** or Windows.  

## Usage (ZIP)

```console
user1@linuxbox:~/Desktop$ g++ pdvzip.cpp -O2 -s -o pdvzip
user1@linuxbox:~/Desktop$ ./pdvzip

Usage: pdvzip <png> <zip/jar>
       pdvzip --info

user1@linuxbox:~/Desktop$ ./pdvzip boat_lake.png coding_pdf_doc.zip

Created PNG-ZIP polyglot image file: pzip_55183.png 4038367 Bytes.

Complete!

You can now post this image on the relevant supported platforms.

``` 
## Usage (JAR)

```console
user1@linuxbox:~/Desktop$ ./pdvzip blue_bottle.png hello_world.jar

Created PNG-JAR polyglot image file: pzip_19662.png 1016336 Bytes.

Complete!

You can now post this image on the relevant supported platforms.

```

## Extracting Your Embedded File(s)  
**Important:** When saving images from **X/Twitter**, click the image in the post to **fully expand it**, before saving.  

***The following section deals with the execution/extraction of embedded ZIP files.  [***JAR***](https://github.com/CleasbyCode/pdvzip/tree/main?tab=readme-ov-file#executing-embedded-jar-files) files are covered later.***

**Linux** ***(Make sure image file has executable permissions)***
```console

user1@linuxbox:~/Desktop$ chmod +x pzip_55183.png
user1@linuxbox:~/Desktop$ ./pzip_55183.png

```  
Alternative execution (Linux).  Using ***wget*** to download & run the image directly from the hosting site.  
**X/Twitter** *wget* example: **Image with embedded python script**.
```console

wget -O Fibo.png "https://pbs.twimg.com/media/GLXTYeCWMAAA6B_";chmod +x Fibo.png;./Fibo.png

```   

**Windows** ***(Rename the image file extension to '.cmd')***
```console

G:\demo> ren pzip_55183.png pzip_55183.cmd
G:\demo> .\pzip_55183.cmd

```
Alternative execution (Windows).  Using ***iwr*** to download & run the image directly from the hosting site.  
**Flickr** *iwr* example: **Image with embedded mp4 video file.**
```console

iwr -o swing.cmd "https://live.staticflickr.com/65535/53660445495_16a6880388_o_d.png";.\swing.cmd

```

Opening the cmd file from the desktop, on its first run, Windows may display a security warning.  
Clear this by clicking '***More info***' then select '***Run anyway***'.  

To avoid security warnings, run the image file from a Windows console, as shown in the above example.  

For common video/audio files, Linux will use the ***vlc*** or ***mpv*** media player. Windows uses the default media player.  
PDF, Linux will use ***evince*** or ***firefox***. Windows uses the default PDF viewer.  
Python, Linux & Windows use ***python3*** to run these programs.  
PowerShell, Linux uses ***pwsh*** (if installed), Windows uses either ***powershell.exe*** or ***pwsh.exe*** to run these scripts.
Folder, Linux uses **xdg-open**, Windows uses **powershell.exe** with II (**Invoke-Item**) command, to open zipped folders.

For any other file type, Linux & Windows will rely on the operating system's set default method/application.  
Obviously, the compressed/embedded file needs to be compatible with the operating system you run it on.

If the compressed/embedded file type is PowerShell, Python, Shell script or a Windows/Linux executable,  
pdvzip will give you the option to provide command-line arguments for your file, if required.  

Make sure to enclose arguments containing spaces, such as file & directory names, within "quotation" marks. 
```console
user1@linuxbox:~/Desktop$ ./pdvzip my_cover_image.png jdvrif_linux_executable.zip

For this file type you can provide command-line arguments here, if required.

Linux: -e ../my_cover_image.jpg "../my document file.pdf"

```
Also, be aware when using arguments, you are always working from within the subdirectory "*pdvzip_extracted*".
  
To just get access to the file(s) within the ZIP archive, rename the '*.png*' file extension to '*.zip*'.  
Treat the ZIP archive as read-only, do not add or remove files from the PNG-ZIP polyglot file.  

## Executing Embedded JAR Files  

Linux Option 1:
```console
user1@linuxbox:~/Desktop$ java -jar pzip_19662.png
```
Linux Option 2:
```console
user1@linuxbox:~/Desktop$ chmod +x pzip_19662.png
user1@linuxbox:~/Desktop$ ./pzip_19662.png
```
Windows Option 1:
```console
PS C:\Users\Nick\Desktop\jar_demo> java -jar .\pzip_19662.png
```
Windows Option 2:
```console
PS C:\Users\Nick\Desktop\jar_demo> ren .\pzip_19662.png .\pzip_19662.cmd
PS C:\Users\Nick\Desktop\jar_demo> .\pzip_19662.cmd
```
## PNG Image Requirements for Arbitrary Data Preservation


PNG file size (image + embedded content) must not exceed the hosting site's size limits.  
The site will either refuse to upload your image or it will convert your image to ***jpg***, such as X/Twitter.

**Dimensions:**

The following dimension size limits are specific to **pdvzip** and not necessarily the extact hosting site's size limits.
These dimension size limits are for compatibility reasons, allowing it to work with all the above listed platforms.

**PNG-32/24 (Truecolor)**

Image dimensions can be set between a minimum of ***68 x 68*** and a maximum of ***899 x 899***.

*Note: Images that are created & saved within your image editor as **PNG-32/24** that are either
black & white/grayscale, images with 256 colors or less, will be converted by **X/Twitter** to
**PNG-8** and you will lose the embedded content. If you want to use a simple "single" color
**PNG-32/24** image, then fill an area with a gradient color instead of a single solid color. 
**X/Twitter** should then keep the image as **PNG-32/24**. [**(Example).**](https://twitter.com/CleasbyCode/status/1694992647121965554)*
    
**PNG-8 (Indexed-color)**

Image dimensions can be set between a minimum of ***68 x 68*** and a maximum of ***4096 x 4096***.
        
**PNG Chunks:**  

With **X/Twitter**, for example, you can overfill the following PNG chunks with arbitrary data,  
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

***bKGD, cHRM, gAMA, hIST,***  
***iCCP,*** (Limited chunk. Only 10KB max. with X/Twitter).  
***IDAT,*** (Use as last IDAT chunk, after the final image IDAT chunk).  
***PLTE,*** (Use only with PNG-32/24 images).  
***pHYs, sBIT, sPLT, sRGB,***   
***tRNS. (Not recommended, may distort image).***  

*Other platforms may differ in what chunks they preserve and which ones you can overfill.*
  
pdvzip uses the chunks ***iCCP*** (stores extraction script) and ***IDAT*** (stores the ZIP/JAR file) for your arbitrary data.

## ZIP/JAR File Size & Other Important Information

To work out the maximum ZIP/JAR file size, start with the size limit, minus the image size, minus 1500 bytes (extraction script size).  
  
X/Twitter example: (5MB limit) 5,242,880 - (307,200 [image] + 1500 [extraction script]) = 4,934,180 bytes available for your ZIP/JAR file.  

* Make sure your ZIP/JAR file is a standard ZIP/JAR archive, compatible with Linux unzip & Windows Explorer.
* Do not include other .zip files within the main ZIP archive. (.rar files are ok).
* Do not include other pdvzip created PNG image files within the main ZIP archive, as they are essentially .zip files.
* Use file extensions for your file(s) within the ZIP archive: *my_doc.pdf*, *my_video.mp4*, *my_program.py*, etc.
  
  A file without an extension within a ZIP archive will be considered a Linux executable.      
* **Paint.net** application is recommended for easily creating compatible PNG image files.  

My other programs you may find useful:-

* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [pdvrdt: CLI tool to encrypt, compress & embed any file type within a PNG image.](https://github.com/CleasbyCode/pdvrdt)
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)  

##
