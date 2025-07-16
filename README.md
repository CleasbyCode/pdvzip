# pdvzip  

Embed a ***ZIP*** or ***JAR*** file within a ***PNG*** image to create a ***tweetable*** and "[***executable***](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files)" ***PNG*** polyglot file.  
Share the image on ***X-Twitter*** and a few other compatible platforms, which retains the embedded archive.  

Based on the similar idea by [***David Buchanan***](https://www.da.vidbuchanan.co.uk/), from his original ***Python*** program [***tweetable-polyglot-png***](https://github.com/DavidBuchanan314/tweetable-polyglot-png),  
***pdvzip*** uses different methods for [***storing***](https://github.com/CleasbyCode/pdvzip#png-image-requirements-for-arbitrary-data-preservation) and [***extracting***](https://github.com/CleasbyCode/pdvzip#extracting-your-embedded-files) embedded files within a ***PNG*** image.  
  
![Demo Image](https://github.com/CleasbyCode/pdvzip/blob/main/demo_image/GrfkF1uWQAAgu7o.png)  
***Image credit:*** [***@obeca***](https://x.com/obeca)  

The ***Linux/Windows*** extraction script is stored within the ***iCCP*** chunk of the ***PNG*** image. The embedded ***ZIP/JAR*** file is stored within its own ***IDAT*** chunk, which will be the last ***IDAT*** chunk of the image file.  

With ***pdvzip***, you can embed a ***ZIP/JAR*** file up to a maximum size of ***2GB** (cover image + archive file). Compatible sites, ***listed below***, have their own ***much smaller*** size limits.

## Compatible Platforms
*Posting size limit measured by the combined size of the cover image + compressed data file.* 

* ***X-Twitter*** (**5MB**), ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***ImgPile*** (**8MB**).  

*Image dimension size limits:*  

* ***PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max.
* ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max.

*Try the ***pdvzip*** Web App [***here***](https://cleasbycode.co.uk/pdvzip/index/) for a convenient alternative to downloading and compiling the CLI source code. Web file uploads are limited to 20MB.* 

## Usage (Linux)

```console
user1@mx:~/Downloads/pdvzip-main/src$ chmod +x compile_pdvzip.sh
user1@mx:~/Downloads/pdvzip-main/src$ ./compile_pdvzip.sh
user1@mx:~/Downloads/pdvzip-main/src$ Compilation successful. Executable 'pdvzip' created.
user1@mx:~/Downloads/pdvzip-main/src$ sudo cp pdvzip /usr/bin

user1@mx:~/Desktop$ pdvzip

Usage: pdvzip <cover_image> <zip/jar>
       pdvzip --info

user1@mx:~/Desktop$ pdvzip my_cover_image.png document_pdf.zip

Created PNG-ZIP polyglot image file: pzip_55183.png (4038367 bytes).

Complete!

user1@mx:~/Desktop$ pdvzip my_cover_image.png hello_world.jar

Created PNG-JAR polyglot image file: pjar_19662.png (1016336 bytes).

Complete!

``` 
## Extracting Your Embedded File(s)  
**Important:** When saving images from ***X-Twitter***, click the image in the post to ***fully expand it***, before saving.  

The following section covers the extraction of embedded ***ZIP*** files. ***JAR*** files are covered later.

***pdvzip*** (for ***Linux***) will attempt to ***automatically set executable permissions*** on newly created polyglot image files.  

You will need to manually set executable permissions using ***chmod*** on these polyglot images downloaded from hosting sites or copied from another machine.

https://github.com/user-attachments/assets/2c545745-279b-4e07-83aa-2ce5d0b78c90

***Linux - using bash (or sh) shell environment.***
```console

user1@mx:~/Desktop$ ./pzip_55183.png

```
**For any other Linux shell environment, you will probably need to invoke bash (or sh) to run the image file.**
```console

mx% bash ./pzip_55183.png 

``` 
Alternative extraction (***Linux***).  Using ***wget*** to download and run the image directly from the hosting site.  
***X-Twitter*** ***wget*** example: **Image with embedded ***python*** script**.
```console

wget -O Fibo.png "https://pbs.twimg.com/media/GLXTYeCWMAAA6B_.png";chmod +x Fibo.png;bash ./Fibo.png

```   

**Windows** ***(Rename the image file extension to '.cmd')***
```console

G:\demo> ren pzip_55183.png pzip_55183.cmd
G:\demo> .\pzip_55183.cmd

```
Alternative extraction (***Windows***).  Using ***iwr*** to download and run the image directly from the hosting site.  
***Flickr*** ***iwr*** example: **Image with embedded mp4 video file.**
```console

iwr -o swing.cmd "https://live.staticflickr.com/65535/54025688614_2f9d474cba_o_d.png";.\swing.cmd

```

Opening the ***.cmd*** file from the desktop, on its first run, ***Windows*** may display a security warning.  
Clear this by clicking '***More info***' then select '***Run anyway***'.  

To avoid security warnings, run the file from a ***Windows console***, as shown in the above example.  

***The file (or folder) within the ZIP archive that appears first within the ZIP file record, determines what extraction script, based on file type, is used.***

For common ***video & audio*** files, ***Linux*** will use the ***vlc*** or ***mpv*** media player. ***Windows*** uses the default media player.  

***PDF*** - ***Linux*** will use ***evince*** or ***firefox***. ***Windows*** uses the default ***PDF*** viewer.  
***Python*** - ***Linux*** & ***Windows*** use ***python3*** to run these programs.  
***PowerShell*** - ***Linux*** uses ***pwsh*** (if installed), ***Windows*** uses either ***powershell.exe*** or ***pwsh.exe*** to run these scripts.
***Folder*** - ***Linux*** uses ***xdg-open***, ***Windows*** uses ***powershell.exe*** with II (***Invoke-Item***) command, to open zipped folders.

For any other file type within your ***ZIP*** file, ***Linux*** & ***Windows*** will rely on the operating system's set default method/application. Obviously, the compressed/embedded file needs to be compatible with the operating system you run it on.

If the archive file is ***JAR*** or the compressed file type within the ***ZIP*** archive is ***PowerShell***, ***Python***, ***Shell Script*** or a ***Windows/Linux Executable, pdvzip*** will give you the option to provide command-line arguments for your file, if required. 

The ***command-line arguments*** will be added to the ***Linux/Windows*** extraction script, embedded within the ***iCCP*** chunk of your ***PNG*** cover image. 

Make sure to enclose arguments containing spaces, such as file & directory names, within "quotation" marks. e.g.
```console
user1@mx:~/Desktop$ ./pdvzip my_cover_image.png jdvrif_linux_executable.zip

For this file type you can provide command-line arguments here, if required.

Linux: -e ../my_cover_image.jpg "../my document file.pdf"

```
Also, be aware when using arguments for the compressed ***ZIP*** file types (not ***JAR***), you are always working from within the subdirectory "***pdvzip_extracted***".  

https://github.com/user-attachments/assets/dbff15e2-87b0-4fe5-a640-3420d356020e

To just get access to the file(s) within the ***ZIP*** archive, rename the '***.png***' file extension to '***.zip***'.  
Treat the ***ZIP*** archive as read-only, do not add or remove files from the ***PNG-ZIP*** polyglot file.  

## Executing Embedded Java Programs

***Linux Option 1:***
```console
user1@mx:~/Desktop$ java -jar pjar_19662.png
Note: If you use this method to run your embedded Java program, you will have to manually add command-line
      arguments (if required) to the end of the command, as your embedded arguments will not work with
      this method. e.g.
      user1@mx:~/Desktop$ java -jar ./pjar_19662.png -u john_s -a 42 -f "John Smith"
```
***Linux Option 2a, using bash (or sh) shell environment:***
```console
user1@mx:~/Desktop$ ./pjar_19662.png
Note: This method will execute the embedded Java program and also use any embedded
      command-line arguments with the Java program.
```
***Linux Option 2b, using any other shell environment, you will need to invoke bash (or sh) to execute the image:***
```console
mx% bash ./pjar_19662.png
```
***Windows Option 1:***
```console
PS C:\Users\Nick\Desktop\jar_demo> java -jar .\pjar_19662.png 
Note: If you use this method to run your embedded Java program, you will have to manually add command-line
      arguments (if required) to the end of the command, as your embedded arguments will not work with
      this method. e.g.
      PS C:\Users\Nick\Desktop\jar_demo> java -jar .\pjar_19662.png -u john_s -a 42 -f "John Smith"
```
***Windows Option 2:***
```console
PS C:\Users\Nick\Desktop\jar_demo> ren .\pjar_19662.png .\pjar_19662.cmd
PS C:\Users\Nick\Desktop\jar_demo> .\pjar_19662.cmd
Note: This method will execute the embedded Java program and will also use any
      embedded command-line arguments with the Java program.
```
https://github.com/user-attachments/assets/9451ad50-4c7c-4fa3-a1be-3854189bde00

## PNG Image Requirements for Arbitrary Data Preservation

***PNG*** file size (image + archive file) must not exceed the platform's size limits.  
The site will either refuse to upload your image or it will convert your image to ***jpg***, such as ***X-Twitter***.

***Dimensions:***

The following dimension size limits are specific to ***pdvzip*** and not necessarily the extact hosting site's size limits.
These dimension size limits are for compatibility reasons, allowing it to work with all the above listed platforms.

***PNG-32/24 (Truecolor)***

Image dimensions can be set between a minimum of **68 x 68** and a maximum of **900 x 900**.

*Note: Images that are created & saved within your image editor as ***PNG-32/24*** that are either
black & white/grayscale, images with 256 colors or less, will be converted by ***X-Twitter*** to
***PNG-8*** and you will lose the embedded content. If you want to use a simple "single" color
***PNG-32/24*** image, then fill an area with a gradient color instead of a single solid color. 
***X-Twitter*** should then keep the image as ***PNG-32/24***. [***(Example).***](https://twitter.com/CleasbyCode/status/1694992647121965554)*
    
***PNG-8 (Indexed-color)***

Image dimensions can be set between a minimum of **68 x 68** and a maximum of **4096 x 4096**.
        
***PNG Chunks:***  

For example, with ***X-Twitter*** you can ***overfill*** the following ***PNG*** chunks with arbitrary data,  
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

***bKGD, cHRM, gAMA, hIST,***  
***iCCP,*** (Limited size chunk. Only **10KB** Max. with ***X-Twitter***).  
***IDAT,*** (Use as last ***IDAT*** chunk, after the final image ***IDAT*** chunk).  
***PLTE,*** (Use only with ***PNG-32/24*** images).  
***pHYs, sBIT, sPLT, sRGB,***   
***tRNS. (Use only with ***PNG-32*** images).***  

*Other platforms may differ in what chunks they preserve and which ones you can overfill.*
  
***pdvzip*** uses the chunks ***iCCP*** (stores extraction script) and ***IDAT*** (stores the ***ZIP/JAR*** file) for your arbitrary data.

## ***ZIP/JAR*** File Size & Other Important Information

To work out the maximum ***ZIP/JAR*** file size, start with the size limit, minus the image size, minus ***1500*** bytes (extraction script size).  
  
***X-Twitter*** example: (**5MB** limit) **5,242,880** - (**307,200** [image] + **1500** [extraction script]) = **4,934,180 bytes** available for your ***ZIP/JAR*** file.  

* Make sure your ***ZIP/JAR*** file is a standard ***ZIP/JAR*** archive, compatible with ***Linux*** unzip & ***Windows*** Explorer.
* Do not include more than one ***.zip*** file within the main ***ZIP*** archive. (***.rar*** files are ok).
* Do not include other ***pdvzip*** created ***PNG*** image files within the main ***ZIP*** archive, as they are essentially ***.zip*** files.
* Use file extensions for your file(s) within the ***ZIP*** archive: ***my_doc.pdf***, ***my_video.mp4***, ****my_program.py****, etc.
  
  A file without an extension within a ***ZIP*** archive will be considered a ***Linux*** executable.      
* ***Paint.net*** application is recommended for easily creating compatible ***PNG*** image files.  

## Third-Party Libraries

This project includes the following third-party library:

- **LodePNG** by Lode Vandevenne
  - License: zlib/libpng (see [***LICENSE***](https://github.com/lvandeve/lodepng/blob/master/LICENSE) file)
  - Copyright (c) 2005-2024 Lode Vandevenne

##
