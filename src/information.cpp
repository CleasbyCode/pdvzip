#include "information.h"
#include <iostream>

void displayInfo() {
	std::cout << R"(
PNG Data Vehicle ZIP/JAR Edition (PDVZIP v3.3). 
Created by Nicholas Cleasby (@CleasbyCode) 6/08/2022.

Use PDVZIP to embed a ZIP/JAR file within a PNG image, 
to create a tweetable and "executable" PNG-ZIP/JAR polyglot file.
		
The supported hosting sites will retain the embedded archive within the PNG image.  
		
PNG image size limits are platform dependant:  

X/Twitter (5MB), Flickr (200MB), Imgbb (32MB), PostImage (32MB), ImgPile (8MB).

Once the ZIP file has been embedded within a PNG image, it can be shared on your chosen
hosting site or 'executed' whenever you want to access the embedded file(s).

pdvzip (Linux) will attempt to automatically set executable permissions on newly created polyglot image files.
You will need to manually set executable permissions using chmod on these polyglot images downloaded from hosting sites.
		
From a Linux terminal: ./pzip_image.png (chmod +x pzip_image.png, if required).
From a Windows terminal: First, rename the '.png' file extension to '.cmd', then .\pzip_image.cmd 

For common video/audio files, Linux uses the media player vlc or mpv. Windows uses the set default media player.
PDF, Linux uses either evince or firefox. Windows uses the set default PDF viewer.
Python, Linux & Windows use python3 to run these programs.
PowerShell, Linux uses pwsh command (if PowerShell installed). 
Depending on the installed version of PowerShell, Windows uses either pwsh.exe or powershell.exe, to run these scripts.
Folder, Linux uses xdg-open, Windows uses powershell.exe with II (Invoke-Item) command, to open zipped folders.

For any other media type/file extension, Linux & Windows will rely on the operating system's method or set default application for those files.

PNG Image Requirements for Arbitrary Data Preservation

PNG file size (image + embedded content) must not exceed the hosting site's size limits.
The site will either refuse to upload your image or it will convert your image to jpg, such as X/Twitter.

Dimensions:

The following dimension size limits are specific to pdvzip and not necessarily the extact hosting site's size limits.

PNG-32/24 (Truecolor)

Image dimensions can be set between a minimum of 68x68 and a maximum of 900x900.
These dimension size limits are for compatibility reasons, allowing it to work with all the above listed platforms.

Note: Images that are created & saved within your image editor as PNG-32/24 that are either 
black & white/grayscale, images with 256 colours or less, will be converted by X/Twitter to 
PNG-8 and you will lose the embedded content. If you want to use a simple "single" colour PNG-32/24 image,
then fill an area with a gradient colour instead of a single solid colour.
X/Twitter should then keep the image as PNG-32/24.

PNG-8 (Indexed-colour)

Image dimensions can be set between a minimum of 68x68 and a maximum of 4096x4096.

PNG Chunks:

For example, with X/Twitter, you can overfill the following PNG chunks with arbitrary data, 
in which the platform will preserve as long as you keep within the image dimension & file size limits.  

Other platforms may differ in what chunks they preserve and which you can overfill.

bKGD, cHRM, gAMA, hIST,
iCCP, (Only 10KB max. with X/Twitter).
IDAT, (Use as last IDAT chunk, after the final image IDAT chunk).
PLTE, (Use only with PNG-32 & PNG-24 for arbitrary data).
pHYs, sBIT, sPLT, sRGB,
tRNS. (PNG-32 only).

This program uses the iCCP (extraction script) and IDAT (zip file) chunk names for storing arbitrary data.

ZIP File Size & Other Information

To work out the maximum ZIP file size, start with the hosting site's size limit,  
minus your PNG image size, minus 1500 bytes (extraction script size).   

X/Twitter example: (5MB Image Limit) 5,242,880 - (image size 307,200 + extraction script size 1500) = 4,934,180 bytes available for your ZIP file.

Make sure ZIP file is a standard ZIP archive, compatible with Linux unzip & Windows Explorer.
Do not include other .zip files within the main ZIP archive. (.rar files are ok).
Do not include other pdvzip created PNG image files within the main ZIP archive, as they are essentially .zip files.
Use file extensions for your media file within the ZIP archive: my_doc.pdf, my_video.mp4, my_program.py, etc.
A file without an extension will be treated as a Linux executable.
Paint.net application is recommended for easily creating compatible PNG image files.
 
)";
}
